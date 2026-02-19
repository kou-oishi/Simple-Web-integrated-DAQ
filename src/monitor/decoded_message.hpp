#pragma once

#include <any>
#include <cstdint>

struct DecodedMessage {
  uint64_t event_index = 0;
  std::any payload;
};

