#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

volatile std::sig_atomic_t g_running = 1;
volatile std::sig_atomic_t g_listen_fd = -1;

void handle_signal(int /*signum*/) {
  g_running = 0;
  if (g_listen_fd >= 0) {
    ::close(static_cast<int>(g_listen_fd));
    g_listen_fd = -1;
  }
}

struct Config {
  uint8_t board_id = 0;
  uint8_t channel_min = 0;
  uint8_t channel_max = 0;
  uint16_t port = 9000;
  uint32_t interval_ms = 1000;
  std::string bind_addr = "0.0.0.0";
};

void print_usage(const char* prog) {
  std::cerr << "Usage: " << prog
            << " --board-id <0-7> (--channel-id <0-31> | --channel-min <0-31> --channel-max <0-31>) [--port <1-65535>]"
            << " [--interval-ms <>=1>] [--bind <IPv4>]\n";
}

bool parse_u32(const std::string& s, uint32_t& out) {
  try {
    size_t pos = 0;
    unsigned long v = std::stoul(s, &pos, 10);
    if (pos != s.size() || v > std::numeric_limits<uint32_t>::max()) {
      return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_args(int argc, char** argv, Config& cfg) {
  bool has_board = false;
  bool has_channel_id = false;
  bool has_channel_min = false;
  bool has_channel_max = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto require_value = [&](const char* opt) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << opt << "\n";
        return nullptr;
      }
      return argv[++i];
    };

    if (arg == "--board-id") {
      const char* val = require_value("--board-id");
      if (!val) return false;
      uint32_t tmp = 0;
      if (!parse_u32(val, tmp) || tmp > 7) {
        std::cerr << "Invalid --board-id: " << val << " (expected 0-7)\n";
        return false;
      }
      cfg.board_id = static_cast<uint8_t>(tmp);
      has_board = true;
    } else if (arg == "--channel-id") {
      const char* val = require_value("--channel-id");
      if (!val) return false;
      uint32_t tmp = 0;
      if (!parse_u32(val, tmp) || tmp > 31) {
        std::cerr << "Invalid --channel-id: " << val << " (expected 0-31)\n";
        return false;
      }
      cfg.channel_min = static_cast<uint8_t>(tmp);
      cfg.channel_max = static_cast<uint8_t>(tmp);
      has_channel_id = true;
    } else if (arg == "--channel-min") {
      const char* val = require_value("--channel-min");
      if (!val) return false;
      uint32_t tmp = 0;
      if (!parse_u32(val, tmp) || tmp > 31) {
        std::cerr << "Invalid --channel-min: " << val << " (expected 0-31)\n";
        return false;
      }
      cfg.channel_min = static_cast<uint8_t>(tmp);
      has_channel_min = true;
    } else if (arg == "--channel-max") {
      const char* val = require_value("--channel-max");
      if (!val) return false;
      uint32_t tmp = 0;
      if (!parse_u32(val, tmp) || tmp > 31) {
        std::cerr << "Invalid --channel-max: " << val << " (expected 0-31)\n";
        return false;
      }
      cfg.channel_max = static_cast<uint8_t>(tmp);
      has_channel_max = true;
    } else if (arg == "--port") {
      const char* val = require_value("--port");
      if (!val) return false;
      uint32_t tmp = 0;
      if (!parse_u32(val, tmp) || tmp == 0 || tmp > 65535) {
        std::cerr << "Invalid --port: " << val << " (expected 1-65535)\n";
        return false;
      }
      cfg.port = static_cast<uint16_t>(tmp);
    } else if (arg == "--interval-ms") {
      const char* val = require_value("--interval-ms");
      if (!val) return false;
      uint32_t tmp = 0;
      if (!parse_u32(val, tmp) || tmp == 0) {
        std::cerr << "Invalid --interval-ms: " << val << " (expected >=1)\n";
        return false;
      }
      cfg.interval_ms = tmp;
    } else if (arg == "--bind") {
      const char* val = require_value("--bind");
      if (!val) return false;
      cfg.bind_addr = val;
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      return false;
    }
  }

  if (!has_board) {
    std::cerr << "--board-id is required\n";
    return false;
  }

  const bool has_range = has_channel_min || has_channel_max;
  if (has_channel_id && has_range) {
    std::cerr << "Use either --channel-id or --channel-min/--channel-max, not both\n";
    return false;
  }
  if (!has_channel_id && !(has_channel_min && has_channel_max)) {
    std::cerr << "Specify --channel-id or both --channel-min and --channel-max\n";
    return false;
  }
  if (cfg.channel_min > cfg.channel_max) {
    std::cerr << "Invalid channel range: channel-min must be <= channel-max\n";
    return false;
  }
  return true;
}

int create_listen_socket(const Config& cfg) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    throw std::runtime_error(std::string("socket failed: ") + std::strerror(errno));
  }

  int yes = 1;
  if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    ::close(fd);
    throw std::runtime_error(std::string("setsockopt(SO_REUSEADDR) failed: ") + std::strerror(errno));
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(cfg.port);
  if (::inet_pton(AF_INET, cfg.bind_addr.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    throw std::runtime_error("invalid --bind IPv4 address: " + cfg.bind_addr);
  }

  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    throw std::runtime_error(std::string("bind failed: ") + std::strerror(errno));
  }

  if (::listen(fd, 1) < 0) {
    ::close(fd);
    throw std::runtime_error(std::string("listen failed: ") + std::strerror(errno));
  }

  return fd;
}

uint64_t build_word(uint8_t board_id, uint8_t channel_id, uint64_t value56) {
  const uint64_t board = (static_cast<uint64_t>(board_id) & 0x7ULL) << 61;
  const uint64_t channel = (static_cast<uint64_t>(channel_id) & 0x1FULL) << 56;
  const uint64_t value = value56 & 0x00FFFFFFFFFFFFFFULL;
  return board | channel | value;
}

uint64_t host_to_be64(uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return (static_cast<uint64_t>(htonl(static_cast<uint32_t>(value & 0xFFFFFFFFULL))) << 32) |
         htonl(static_cast<uint32_t>(value >> 32));
#else
  return value;
#endif
}

bool send_all(int fd, const void* buf, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(buf);
  size_t sent = 0;
  while (sent < len) {
    const ssize_t n = ::send(fd, p + sent, len - sent, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Config cfg;
  if (!parse_args(argc, argv, cfg)) {
    print_usage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);
  std::signal(SIGPIPE, SIG_IGN);

  try {
    const int listen_fd = create_listen_socket(cfg);
    g_listen_fd = listen_fd;
    std::cout << "Listening on " << cfg.bind_addr << ":" << cfg.port
              << " (board=" << static_cast<int>(cfg.board_id)
              << ", channel=[" << static_cast<int>(cfg.channel_min) << ".."
              << static_cast<int>(cfg.channel_max) << "]"
              << ", interval_ms=" << cfg.interval_ms << ")\n";

    while (g_running) {
      sockaddr_in peer{};
      socklen_t peer_len = sizeof(peer);
      const int client_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &peer_len);
      if (client_fd < 0) {
        if (!g_running) break;
        if (errno == EINTR && !g_running) break;
        if (errno == EINTR) continue;
        std::cerr << "accept failed: " << std::strerror(errno) << "\n";
        continue;
      }

      char peer_ip[INET_ADDRSTRLEN] = {0};
      ::inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip));
      std::cout << "Client connected: " << peer_ip << ":" << ntohs(peer.sin_port) << "\n";

      std::mt19937_64 rng(std::random_device{}());
      std::uniform_int_distribution<uint64_t> value_dist(0, 0x00FFFFFFFFFFFFFFULL);
      std::uniform_int_distribution<uint32_t> channel_dist(cfg.channel_min, cfg.channel_max);

      while (g_running) {
        const uint64_t data56 = value_dist(rng);
        const uint8_t channel_id = static_cast<uint8_t>(channel_dist(rng));
        const uint64_t word = build_word(cfg.board_id, channel_id, data56);
        const uint64_t be_word = host_to_be64(word);

        if (!send_all(client_fd, &be_word, sizeof(be_word))) {
          std::cout << "Client disconnected\n";
          break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.interval_ms));
      }

      ::close(client_fd);
    }

    if (g_listen_fd >= 0) {
      ::close(listen_fd);
      g_listen_fd = -1;
    }
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }

  return 0;
}
