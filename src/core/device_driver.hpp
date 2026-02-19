#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

enum class ReadStatus {
  kOk,
  kTimeout,
  kDisconnected,
  kError,
};

class IDeviceDriver {
 public:
  virtual ~IDeviceDriver() = default;

  virtual bool connect_device() = 0;
  virtual void disconnect_device() = 0;
  virtual ReadStatus read_bytes(std::vector<uint8_t>& out_bytes, size_t max_bytes, int timeout_ms) = 0;
};
