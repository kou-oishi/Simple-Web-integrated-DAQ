#pragma once

#include <csignal>
#include <functional>

#include "core/daq_types.hpp"

using FramePublishCallback = std::function<void(const FrameRecord&)>;
using StartupStatusCallback = std::function<void(bool, const std::string&)>;
using RuntimeErrorCallback = std::function<void(const std::string&)>;

int RunDaqCore(const DaqConfig& cfg,
               volatile std::sig_atomic_t& stop_requested,
               const FramePublishCallback& on_frame_ready = FramePublishCallback{},
               const StartupStatusCallback& on_startup_status = StartupStatusCallback{},
               const RuntimeErrorCallback& on_runtime_error = RuntimeErrorCallback{});
