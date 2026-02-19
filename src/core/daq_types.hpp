#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/defaults.hpp"

struct DeviceSpec {
  std::string frontend;
  uint8_t board_id = 0;
  std::string host;
  uint16_t port = 0;
};

struct DaqConfig {
  std::string output_dir;
  uint32_t run_start = daq_defaults::kRunStart;
  uint32_t events_per_file = daq_defaults::kEventsPerFile;
  std::vector<DeviceSpec> devices;
  uint32_t reconnect_ms = daq_defaults::kReconnectMs;
  uint32_t read_timeout_ms = daq_defaults::kReadTimeoutMs;
  uint32_t duration_sec = daq_defaults::kDurationSec;
};

struct FrameRecord {
  std::string source;
  std::vector<uint8_t> payload;
};
