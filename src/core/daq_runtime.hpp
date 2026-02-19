#pragma once

#include <csignal>
#include <functional>

#include "core/daq_types.hpp"

using FramePublishCallback = std::function<void(const FrameRecord&)>;

int RunDaqCore(const DaqConfig& cfg,
               volatile std::sig_atomic_t& stop_requested,
               const FramePublishCallback& on_frame_ready = FramePublishCallback{});
