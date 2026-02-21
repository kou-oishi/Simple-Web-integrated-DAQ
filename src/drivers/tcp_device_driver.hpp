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
    kNetworkToHostWord,
  };

  TcpDeviceDriver(std::string host,
                  uint16_t port,
                  EndianMode endian_mode = EndianMode::kRawBytes,
                  std::size_t network_word_bytes = 8);
  ~TcpDeviceDriver() override;

  bool ConnectDevice() override;
  void DisconnectDevice() override;
  ReadStatus ReadBytes(std::vector<uint8_t>& out_bytes, size_t max_bytes, int timeout_ms) override;

 private:
  static bool IsLittleEndianHost();
  static void AppendNetworkWordAsHost(const uint8_t* network_word, std::size_t word_bytes, std::vector<uint8_t>& out_bytes);

  std::string host_;
  uint16_t port_;
  EndianMode endian_mode_;
  std::size_t network_word_bytes_;
  int sock_fd_;
  std::vector<uint8_t> pending_network_bytes_;
  std::vector<uint8_t> converted_host_bytes_;
};
