#pragma once

#include <cstddef>
#include <cstdint>

namespace daq_defaults {

inline constexpr const char* kControlEndpoint = "ipc:///tmp/simpledaq_ctrl.sock";
inline constexpr const char* kStatusEndpoint = "ipc:///tmp/simpledaq_status.sock";
inline constexpr const char* kDataEndpoint = "ipc:///tmp/simpledaq_data.sock";
inline constexpr const char* kDaqdLogPath = "logs/daqd.log";

inline constexpr uint32_t kRunStart = 0;
inline constexpr uint32_t kEventsPerFile = 100000;
inline constexpr uint32_t kReconnectMs = 1000;
inline constexpr uint32_t kReadTimeoutMs = 500;
inline constexpr uint32_t kDurationSec = 0;
inline constexpr uint32_t kStartupConnectTimeoutSec = 2;
inline constexpr uint32_t kReconnectFailureTimeoutSec = 5;

inline constexpr std::size_t kReadChunkSizeBytes = 8;
inline constexpr int kRunNumberWidth = 5;

inline constexpr bool kMySqlEnabled = true;
inline constexpr const char* kMySqlHost = "127.0.0.1";
inline constexpr uint32_t kMySqlPort = 3306;
inline constexpr const char* kMySqlUser           = "daq";
inline constexpr const char* kMySqlPassword       = "daq";
inline constexpr const char* kMySqlDatabase       = "daq";
inline constexpr const char* kMySqlRunLogTable = "daq_log";
inline constexpr const char* kMySqlSubrunLogTable = kMySqlRunLogTable;

}  // namespace daq_defaults
