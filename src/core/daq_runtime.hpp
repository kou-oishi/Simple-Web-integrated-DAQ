#pragma once

#include <csignal>

#include "core/daq_types.hpp"

int RunDaqCore(const DaqConfig& cfg, volatile std::sig_atomic_t& stop_requested);
