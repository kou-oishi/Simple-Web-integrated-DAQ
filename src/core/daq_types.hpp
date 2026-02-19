#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct DeviceSpec {
  std::string frontend;
  uint8_t board_id = 0;
  std::string host;
  uint16_t port = 0;
};

struct DaqConfig {
  std::string output_dir;
  uint32_t run_start = 0;
  uint32_t events_per_file = 100000;
  std::vector<DeviceSpec> devices;
  uint32_t reconnect_ms = 1000;
  uint32_t read_timeout_ms = 500;
  uint32_t duration_sec = 0;
};

struct FrameRecord {
  std::string source;
  std::vector<uint8_t> payload;
};
