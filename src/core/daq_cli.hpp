#pragma once

#include "core/daq_types.hpp"

void PrintDaqUsage(const char* prog);
bool ParseDaqArgs(int argc, char** argv, DaqConfig& cfg);
