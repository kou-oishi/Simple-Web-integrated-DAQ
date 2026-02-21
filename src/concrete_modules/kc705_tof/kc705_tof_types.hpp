#pragma once

#include <cstdint>

#include "monitor/decoded_event.hpp"
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
#include <RtypesCore.h>
#else
using Double_t = double;
#endif

struct Kc705TofEvent : public DecodedEventBase {
  uint64_t raw_word = 0;
  uint8_t board_id = 0;
  uint8_t channel_id = 0;
  Double_t tof = 0.0;
};
