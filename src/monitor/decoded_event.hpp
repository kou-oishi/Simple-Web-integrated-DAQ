#pragma once

#include <cstdint>

struct DecodedEventBase {
  uint32_t run_number = 0;
  uint32_t subrun_number = 0;
  uint64_t event_number = 0;
};
