#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
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

  virtual bool ConnectDevice() = 0;
  virtual void DisconnectDevice() = 0;
  virtual ReadStatus ReadBytes(std::vector<uint8_t>& out_bytes, size_t max_bytes, int timeout_ms) = 0;
  virtual std::string LastErrorDetail() const = 0;
};
