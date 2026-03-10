#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>

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
  Double_t time = 0.0;
};

inline constexpr std::array<uint8_t, 2> kKc705TofPeriodicChannels = {1, 7};

inline constexpr std::size_t Kc705TofPeriodicChannelCount() {
  return kKc705TofPeriodicChannels.size();
}

inline constexpr bool IsPeriodicChannel(uint8_t channel_id) {
  for (uint8_t periodic_channel : kKc705TofPeriodicChannels) {
    if (periodic_channel == channel_id) {
      return true;
    }
  }
  return false;
}

inline constexpr std::optional<std::size_t> PeriodicChannelIndex(uint8_t channel_id) {
  for (std::size_t i = 0; i < kKc705TofPeriodicChannels.size(); ++i) {
    if (kKc705TofPeriodicChannels[i] == channel_id) {
      return i;
    }
  }
  return std::nullopt;
}
