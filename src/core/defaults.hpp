#pragma once

#include <cstddef>
#include <cstdint>

namespace daq_defaults {

inline constexpr const char* kControlEndpoint = "ipc:///tmp/simpledaq_ctrl.sock";
inline constexpr const char* kStatusEndpoint = "ipc:///tmp/simpledaq_status.sock";
inline constexpr const char* kDataEndpoint = "ipc:///tmp/simpledaq_data.sock";

inline constexpr uint32_t kRunStart = 0;
inline constexpr uint32_t kEventsPerFile = 100000;
inline constexpr uint32_t kReconnectMs = 1000;
inline constexpr uint32_t kReadTimeoutMs = 500;
inline constexpr uint32_t kDurationSec = 0;

inline constexpr std::size_t kReadChunkSizeBytes = 8;
inline constexpr int kRunNumberWidth = 5;

}  // namespace daq_defaults
