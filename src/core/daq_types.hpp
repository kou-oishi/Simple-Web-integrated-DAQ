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
  uint32_t subrun_start = 0;
  bool run_start_specified = false;
  bool allow_existing_run = false;
  uint32_t events_per_file = daq_defaults::kEventsPerFile;
  std::string comment;
  std::vector<DeviceSpec> devices;
  uint32_t reconnect_ms = daq_defaults::kReconnectMs;
  uint32_t read_timeout_ms = daq_defaults::kReadTimeoutMs;
  uint32_t duration_sec = daq_defaults::kDurationSec;
  uint32_t startup_connect_timeout_sec = daq_defaults::kStartupConnectTimeoutSec;
  uint32_t reconnect_failure_timeout_sec = daq_defaults::kReconnectFailureTimeoutSec;
  bool allow_partial_run_on_runtime_disconnect = daq_defaults::kAllowPartialRunOnRuntimeDisconnect;
};

struct FrameRecord {
  std::string source;
  uint32_t run_number = 0;
  uint32_t subrun_number = 0;
  uint64_t event_number = 0;
  std::vector<uint8_t> payload;
};
