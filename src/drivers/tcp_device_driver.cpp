#include "drivers/tcp_device_driver.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <utility>

#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#if defined(__linux__)
#include <netinet/tcp.h>
#endif
#include <unistd.h>

namespace {

constexpr std::size_t kMinConsecutiveReadTimeouts = 60;
constexpr int kReadTimeoutDisconnectWindowMs = 45000;

std::size_t compute_timeout_limit(int timeout_ms) {
  if (timeout_ms <= 0) {
    return kMinConsecutiveReadTimeouts;
  }
  const int timeout_window_count = (kReadTimeoutDisconnectWindowMs + timeout_ms - 1) / timeout_ms;
  if (timeout_window_count <= 0) {
    return kMinConsecutiveReadTimeouts;
  }
  return std::max<std::size_t>(kMinConsecutiveReadTimeouts, static_cast<std::size_t>(timeout_window_count));
}

void configure_keepalive(int fd) {
  const int enable = 1;
  (void)::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));
#if defined(__linux__)
  const int keep_idle_sec = 5;
  const int keep_intvl_sec = 2;
  const int keep_cnt = 3;
  (void)::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle_sec, sizeof(keep_idle_sec));
  (void)::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keep_intvl_sec, sizeof(keep_intvl_sec));
  (void)::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keep_cnt, sizeof(keep_cnt));
#endif
}

std::string format_errno_message(const char* prefix, int err) {
  std::ostringstream oss;
  oss << prefix << " errno=" << err << " (" << std::strerror(err) << ")";
  return oss.str();
}

}  // namespace

TcpDeviceDriver::TcpDeviceDriver(std::string host,
                                 uint16_t port,
                                 EndianMode endian_mode,
                                 std::size_t network_word_bytes)
    : host_(std::move(host)),
      port_(port),
      endian_mode_(endian_mode),
      network_word_bytes_(network_word_bytes == 0 ? 1 : network_word_bytes),
      sock_fd_(-1),
      consecutive_timeouts_(0) {}

TcpDeviceDriver::~TcpDeviceDriver() { DisconnectDevice(); }

bool TcpDeviceDriver::ConnectDevice() {
  DisconnectDevice();
  pending_network_bytes_.clear();
  converted_host_bytes_.clear();
  last_error_detail_.clear();

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* results = nullptr;
  const std::string service = std::to_string(port_);
  const int gai_rc = ::getaddrinfo(host_.c_str(), service.c_str(), &hints, &results);
  if (gai_rc != 0) {
    last_error_detail_ = std::string("getaddrinfo failed: ") + ::gai_strerror(gai_rc);
    return false;
  }

  bool connected = false;
  for (addrinfo* p = results; p != nullptr; p = p->ai_next) {
    const int fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) {
      last_error_detail_ = format_errno_message("socket failed", errno);
      continue;
    }

    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
      configure_keepalive(fd);
      sock_fd_ = fd;
      consecutive_timeouts_ = 0;
      last_error_detail_.clear();
      connected = true;
      break;
    }

    last_error_detail_ = format_errno_message("connect failed", errno);
    ::close(fd);
  }

  ::freeaddrinfo(results);
  return connected;
}

void TcpDeviceDriver::DisconnectDevice() {
  if (sock_fd_ >= 0) {
    // Force immediate close (RST) so peer side can release stale connection state
    // even when no application data has been exchanged yet.
    const linger lin{1, 0};
    (void)::setsockopt(sock_fd_, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
    // Force both directions closed before closing fd so peer can observe
    // disconnect promptly instead of waiting for further I/O.
    (void)::shutdown(sock_fd_, SHUT_RDWR);
    ::close(sock_fd_);
    sock_fd_ = -1;
  }
  consecutive_timeouts_ = 0;
  pending_network_bytes_.clear();
  converted_host_bytes_.clear();
}

ReadStatus TcpDeviceDriver::ReadBytes(std::vector<uint8_t>& out_bytes, size_t max_bytes, int timeout_ms) {
  if (sock_fd_ < 0) {
    last_error_detail_ = "socket is not connected";
    return ReadStatus::kDisconnected;
  }
  if (max_bytes == 0) {
    last_error_detail_ = "max_bytes must be > 0";
    return ReadStatus::kError;
  }
  pollfd pfd{};
  pfd.fd = sock_fd_;
  pfd.events = POLLIN;

  const int poll_rc = ::poll(&pfd, 1, timeout_ms);
  const std::size_t timeout_limit = compute_timeout_limit(timeout_ms);
  if (poll_rc == 0) {
    ++consecutive_timeouts_;
    if (consecutive_timeouts_ >= timeout_limit) {
      std::ostringstream oss;
      oss << "read timeout threshold reached: count=" << consecutive_timeouts_
          << " limit=" << timeout_limit << " timeout_ms=" << timeout_ms;
      last_error_detail_ = oss.str();
      return ReadStatus::kDisconnected;
    }
    std::ostringstream oss;
    oss << "read timeout: count=" << consecutive_timeouts_
        << " limit=" << timeout_limit << " timeout_ms=" << timeout_ms;
    last_error_detail_ = oss.str();
    return ReadStatus::kTimeout;
  }
  if (poll_rc < 0) {
    if (errno == EINTR) {
      ++consecutive_timeouts_;
      if (consecutive_timeouts_ >= timeout_limit) {
        std::ostringstream oss;
        oss << "poll interrupted repeatedly; treating as disconnect: count=" << consecutive_timeouts_
            << " limit=" << timeout_limit;
        last_error_detail_ = oss.str();
        return ReadStatus::kDisconnected;
      }
      last_error_detail_ = "poll interrupted by signal (EINTR)";
      return ReadStatus::kTimeout;
    }
    last_error_detail_ = format_errno_message("poll failed", errno);
    return ReadStatus::kError;
  }
  consecutive_timeouts_ = 0;
  if ((pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
    std::ostringstream oss;
    oss << "poll revents=0x" << std::hex << pfd.revents;
    last_error_detail_ = oss.str();
    return ReadStatus::kDisconnected;
  }

  if (endian_mode_ == EndianMode::kRawBytes) {
    out_bytes.resize(max_bytes);
    const ssize_t n = ::recv(sock_fd_, out_bytes.data(), out_bytes.size(), 0);
    if (n == 0) {
      out_bytes.clear();
      last_error_detail_ = "peer closed connection (recv=0)";
      return ReadStatus::kDisconnected;
    }
    if (n < 0) {
      out_bytes.clear();
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        last_error_detail_ = format_errno_message("recv timeout/interrupted", errno);
        return ReadStatus::kTimeout;
      }
      last_error_detail_ = format_errno_message("recv failed", errno);
      return ReadStatus::kError;
    }
    last_error_detail_.clear();
    out_bytes.resize(static_cast<size_t>(n));
    return ReadStatus::kOk;
  }

  if (converted_host_bytes_.empty()) {
    std::vector<uint8_t> network_chunk(max_bytes);
    const ssize_t n = ::recv(sock_fd_, network_chunk.data(), network_chunk.size(), 0);
    if (n == 0) {
      last_error_detail_ = "peer closed connection (recv=0)";
      return ReadStatus::kDisconnected;
    }
    if (n < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        last_error_detail_ = format_errno_message("recv timeout/interrupted", errno);
        return ReadStatus::kTimeout;
      }
      last_error_detail_ = format_errno_message("recv failed", errno);
      return ReadStatus::kError;
    }
    network_chunk.resize(static_cast<size_t>(n));
    pending_network_bytes_.insert(pending_network_bytes_.end(), network_chunk.begin(), network_chunk.end());

    while (pending_network_bytes_.size() >= network_word_bytes_) {
      AppendNetworkWordAsHost(pending_network_bytes_.data(), network_word_bytes_, converted_host_bytes_);
      pending_network_bytes_.erase(pending_network_bytes_.begin(),
                                   pending_network_bytes_.begin() + static_cast<std::ptrdiff_t>(network_word_bytes_));
    }
  }

  if (converted_host_bytes_.empty()) {
    last_error_detail_.clear();
    return ReadStatus::kOk;
  }

  const size_t out_n = std::min(max_bytes, converted_host_bytes_.size());
  out_bytes.assign(converted_host_bytes_.begin(), converted_host_bytes_.begin() + static_cast<std::ptrdiff_t>(out_n));
  converted_host_bytes_.erase(converted_host_bytes_.begin(), converted_host_bytes_.begin() + static_cast<std::ptrdiff_t>(out_n));
  last_error_detail_.clear();
  return ReadStatus::kOk;
}

std::string TcpDeviceDriver::LastErrorDetail() const { return last_error_detail_; }

bool TcpDeviceDriver::IsLittleEndianHost() {
  const uint16_t probe = 1;
  return reinterpret_cast<const uint8_t*>(&probe)[0] == 1;
}

void TcpDeviceDriver::AppendNetworkWordAsHost(const uint8_t* network_word,
                                                   std::size_t word_bytes,
                                                   std::vector<uint8_t>& out_bytes) {
  if (word_bytes == 0) {
    return;
  }

  out_bytes.reserve(out_bytes.size() + word_bytes);
  if (!IsLittleEndianHost()) {
    out_bytes.insert(out_bytes.end(), network_word, network_word + static_cast<std::ptrdiff_t>(word_bytes));
    return;
  }

  for (std::size_t i = 0; i < word_bytes; ++i) {
    out_bytes.push_back(network_word[word_bytes - 1 - i]);
  }
}
