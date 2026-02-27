#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <getopt.h>
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
  double rate_hz = 1.0;
  std::string bind_addr = "0.0.0.0";
  bool no_data = false;
};

void print_usage(const char* prog) {
  std::cerr << "Usage: " << prog
            << " --board-id <0-7> (--channel-id <0-31> | --channel-min <0-31> --channel-max <0-31>) [--port <1-65535>]"
            << " [--rate-hz <float> >0] [--bind <IPv4>] [--no-data]\n";
  std::cerr << "Options:\n";
  std::cerr << "  -b, --board-id <0-7>       Board ID to encode in outgoing frames (required)\n";
  std::cerr << "  -c, --channel-id <0-31>    Fixed channel ID (mutually exclusive with range options)\n";
  std::cerr << "  -m, --channel-min <0-31>   Minimum channel ID for random range mode\n";
  std::cerr << "  -x, --channel-max <0-31>   Maximum channel ID for random range mode\n";
  std::cerr << "  -p, --port <1-65535>       TCP listen port (default: 9000)\n";
  std::cerr << "  -r, --rate-hz <float>      Mean event rate [/s] for Poisson timing (default: 1.0)\n";
  std::cerr << "  -B, --bind <IPv4>          Bind address (default: 0.0.0.0)\n";
  std::cerr << "  -n, --no-data              Accept TCP connection but do not send any frame\n";
  std::cerr << "  -h, --help                 Show this help\n";
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

bool parse_f64(const std::string& s, double& out) {
  char* end = nullptr;
  errno = 0;
  const double v = std::strtod(s.c_str(), &end);
  if (errno != 0 || end == nullptr || *end != '\0') {
    return false;
  }
  out = v;
  return true;
}

bool parse_args(int argc, char** argv, Config& cfg) {
  bool has_board = false;
  bool has_channel_id = false;
  bool has_channel_min = false;
  bool has_channel_max = false;

  static constexpr option kLongOpts[] = {
      {"board-id", required_argument, nullptr, 'b'},
      {"channel-id", required_argument, nullptr, 'c'},
      {"channel-min", required_argument, nullptr, 'm'},
      {"channel-max", required_argument, nullptr, 'x'},
      {"port", required_argument, nullptr, 'p'},
      {"rate-hz", required_argument, nullptr, 'r'},
      {"bind", required_argument, nullptr, 'B'},
      {"no-data", no_argument, nullptr, 'n'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0},
  };

  optind = 1;
  opterr = 0;
  while (true) {
    const int c = ::getopt_long(argc, argv, ":b:c:m:x:p:r:B:nh", kLongOpts, nullptr);
    if (c == -1) {
      break;
    }
    switch (c) {
    case 'b': {
      uint32_t tmp = 0;
      if (!parse_u32(optarg, tmp) || tmp > 7) {
        std::cerr << "Invalid --board-id: " << optarg << " (expected 0-7)\n";
        return false;
      }
      cfg.board_id = static_cast<uint8_t>(tmp);
      has_board = true;
      break;
    }
    case 'c': {
      uint32_t tmp = 0;
      if (!parse_u32(optarg, tmp) || tmp > 31) {
        std::cerr << "Invalid --channel-id: " << optarg << " (expected 0-31)\n";
        return false;
      }
      cfg.channel_min = static_cast<uint8_t>(tmp);
      cfg.channel_max = static_cast<uint8_t>(tmp);
      has_channel_id = true;
      break;
    }
    case 'm': {
      uint32_t tmp = 0;
      if (!parse_u32(optarg, tmp) || tmp > 31) {
        std::cerr << "Invalid --channel-min: " << optarg << " (expected 0-31)\n";
        return false;
      }
      cfg.channel_min = static_cast<uint8_t>(tmp);
      has_channel_min = true;
      break;
    }
    case 'x': {
      uint32_t tmp = 0;
      if (!parse_u32(optarg, tmp) || tmp > 31) {
        std::cerr << "Invalid --channel-max: " << optarg << " (expected 0-31)\n";
        return false;
      }
      cfg.channel_max = static_cast<uint8_t>(tmp);
      has_channel_max = true;
      break;
    }
    case 'p': {
      uint32_t tmp = 0;
      if (!parse_u32(optarg, tmp) || tmp == 0 || tmp > 65535) {
        std::cerr << "Invalid --port: " << optarg << " (expected 1-65535)\n";
        return false;
      }
      cfg.port = static_cast<uint16_t>(tmp);
      break;
    }
    case 'r': {
      double tmp = 0.0;
      if (!parse_f64(optarg, tmp) || !(tmp > 0.0)) {
        std::cerr << "Invalid --rate-hz: " << optarg << " (expected >0)\n";
        return false;
      }
      cfg.rate_hz = tmp;
      break;
    }
    case 'B':
      cfg.bind_addr = optarg;
      break;
    case 'n':
      cfg.no_data = true;
      break;
    case 'h':
      print_usage(argv[0]);
      std::exit(0);
    case ':':
      std::cerr << "Missing value for option: " << argv[optind - 1] << "\n";
      return false;
    default:
      std::cerr << "Unknown argument: " << argv[optind - 1] << "\n";
      return false;
    }
  }

  if (optind < argc) {
    std::cerr << "Unknown argument: " << argv[optind] << "\n";
    return false;
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
              << ", rate_hz=" << cfg.rate_hz
              << ", no_data=" << (cfg.no_data ? "true" : "false") << ")\n";

    const auto start_time = std::chrono::steady_clock::now();

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
      std::uniform_int_distribution<uint32_t> channel_dist(cfg.channel_min, cfg.channel_max);
      std::exponential_distribution<double> inter_arrival_dist(cfg.rate_hz);

      while (g_running) {
        if (cfg.no_data) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_time).count();
        const uint64_t data56 = (elapsed_ns <= 0) ? 0ULL : (static_cast<uint64_t>(elapsed_ns) / 4ULL);
        const uint8_t channel_id = static_cast<uint8_t>(channel_dist(rng));
        const uint64_t word = build_word(cfg.board_id, channel_id, data56);
        std::array<uint8_t, 12> wire{};
        wire[0] = 0xAA;
        wire[1] = 0x55;
        for (size_t i = 0; i < 8; ++i) {
          wire[2 + i] = static_cast<uint8_t>((word >> ((7U - i) * 8U)) & 0xFFU);
        }
        wire[10] = 0x55;
        wire[11] = 0xAA;

        if (!send_all(client_fd, wire.data(), wire.size())) {
          std::cout << "Client disconnected\n";
          break;
        }

        const double wait_sec = inter_arrival_dist(rng);
        if (wait_sec > 0.0) {
          std::this_thread::sleep_for(std::chrono::duration<double>(wait_sec));
        }
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
