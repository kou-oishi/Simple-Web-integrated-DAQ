#include "drivers/tcp_device_driver.hpp"

#include <cerrno>
#include <cstring>
#include <utility>

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

TcpDeviceDriver::TcpDeviceDriver(std::string host,
                                 uint16_t port,
                                 EndianMode endian_mode,
                                 std::size_t network_word_bytes)
    : host_(std::move(host)),
      port_(port),
      endian_mode_(endian_mode),
      network_word_bytes_(network_word_bytes == 0 ? 1 : network_word_bytes),
      sock_fd_(-1) {}

TcpDeviceDriver::~TcpDeviceDriver() { DisconnectDevice(); }

bool TcpDeviceDriver::ConnectDevice() {
  DisconnectDevice();
  pending_network_bytes_.clear();
  converted_host_bytes_.clear();

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* results = nullptr;
  const std::string service = std::to_string(port_);
  if (::getaddrinfo(host_.c_str(), service.c_str(), &hints, &results) != 0) {
    return false;
  }

  bool connected = false;
  for (addrinfo* p = results; p != nullptr; p = p->ai_next) {
    const int fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) {
      continue;
    }

    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
      sock_fd_ = fd;
      connected = true;
      break;
    }

    ::close(fd);
  }

  ::freeaddrinfo(results);
  return connected;
}

void TcpDeviceDriver::DisconnectDevice() {
  if (sock_fd_ >= 0) {
    ::close(sock_fd_);
    sock_fd_ = -1;
  }
  pending_network_bytes_.clear();
  converted_host_bytes_.clear();
}

ReadStatus TcpDeviceDriver::ReadBytes(std::vector<uint8_t>& out_bytes, size_t max_bytes, int timeout_ms) {
  if (sock_fd_ < 0) {
    return ReadStatus::kDisconnected;
  }
  if (max_bytes == 0) {
    return ReadStatus::kError;
  }
  pollfd pfd{};
  pfd.fd = sock_fd_;
  pfd.events = POLLIN;

  const int poll_rc = ::poll(&pfd, 1, timeout_ms);
  if (poll_rc == 0) {
    return ReadStatus::kTimeout;
  }
  if (poll_rc < 0) {
    if (errno == EINTR) {
      return ReadStatus::kTimeout;
    }
    return ReadStatus::kError;
  }
  if ((pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
    return ReadStatus::kDisconnected;
  }

  if (endian_mode_ == EndianMode::kRawBytes) {
    out_bytes.resize(max_bytes);
    const ssize_t n = ::recv(sock_fd_, out_bytes.data(), out_bytes.size(), 0);
    if (n == 0) {
      out_bytes.clear();
      return ReadStatus::kDisconnected;
    }
    if (n < 0) {
      out_bytes.clear();
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        return ReadStatus::kTimeout;
      }
      return ReadStatus::kError;
    }
    out_bytes.resize(static_cast<size_t>(n));
    return ReadStatus::kOk;
  }

  if (converted_host_bytes_.empty()) {
    std::vector<uint8_t> network_chunk(max_bytes);
    const ssize_t n = ::recv(sock_fd_, network_chunk.data(), network_chunk.size(), 0);
    if (n == 0) {
      return ReadStatus::kDisconnected;
    }
    if (n < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        return ReadStatus::kTimeout;
      }
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
    return ReadStatus::kOk;
  }

  const size_t out_n = std::min(max_bytes, converted_host_bytes_.size());
  out_bytes.assign(converted_host_bytes_.begin(), converted_host_bytes_.begin() + static_cast<std::ptrdiff_t>(out_n));
  converted_host_bytes_.erase(converted_host_bytes_.begin(), converted_host_bytes_.begin() + static_cast<std::ptrdiff_t>(out_n));
  return ReadStatus::kOk;
}

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
