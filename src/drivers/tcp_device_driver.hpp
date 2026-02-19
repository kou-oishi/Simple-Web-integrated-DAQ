#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/device_driver.hpp"

class TcpDeviceDriver : public IDeviceDriver {
 public:
  enum class EndianMode {
    kRawBytes,
    kNetworkToHostU64,
  };

  TcpDeviceDriver(std::string host, uint16_t port, EndianMode endian_mode = EndianMode::kRawBytes);
  ~TcpDeviceDriver() override;

  bool connect_device() override;
  void disconnect_device() override;
  ReadStatus read_bytes(std::vector<uint8_t>& out_bytes, size_t max_bytes, int timeout_ms) override;

 private:
  static uint64_t be64_to_host_u64(uint64_t value);

  std::string host_;
  uint16_t port_;
  EndianMode endian_mode_;
  int sock_fd_;
  std::vector<uint8_t> pending_network_bytes_;
  std::vector<uint8_t> converted_host_bytes_;
};
