#pragma once

#include <any>
#include <cstdint>

struct DecodedMessage {
  uint32_t run_number = 0;
  uint32_t subrun_number = 0;
  uint64_t event_number = 0;
  std::any payload;
};
