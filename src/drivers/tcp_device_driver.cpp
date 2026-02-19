#include "drivers/tcp_device_driver.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <utility>

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

TcpDeviceDriver::TcpDeviceDriver(std::string host, uint16_t port, EndianMode endian_mode)
    : host_(std::move(host)), port_(port), endian_mode_(endian_mode), sock_fd_(-1) {}

TcpDeviceDriver::~TcpDeviceDriver() { disconnect_device(); }

bool TcpDeviceDriver::connect_device() {
  disconnect_device();
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

void TcpDeviceDriver::disconnect_device() {
  if (sock_fd_ >= 0) {
    ::close(sock_fd_);
    sock_fd_ = -1;
  }
  pending_network_bytes_.clear();
  converted_host_bytes_.clear();
}

ReadStatus TcpDeviceDriver::read_bytes(std::vector<uint8_t>& out_bytes, size_t max_bytes, int timeout_ms) {
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

    while (pending_network_bytes_.size() >= 8) {
      uint64_t be_word = 0;
      std::memcpy(&be_word, pending_network_bytes_.data(), sizeof(be_word));
      const uint64_t host_word = be64_to_host_u64(be_word);
      const uint8_t* p = reinterpret_cast<const uint8_t*>(&host_word);
      converted_host_bytes_.insert(converted_host_bytes_.end(), p, p + sizeof(host_word));
      pending_network_bytes_.erase(pending_network_bytes_.begin(), pending_network_bytes_.begin() + 8);
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

uint64_t TcpDeviceDriver::be64_to_host_u64(uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return (static_cast<uint64_t>(ntohl(static_cast<uint32_t>(value & 0xFFFFFFFFULL))) << 32) |
         ntohl(static_cast<uint32_t>(value >> 32));
#else
  return value;
#endif
}
