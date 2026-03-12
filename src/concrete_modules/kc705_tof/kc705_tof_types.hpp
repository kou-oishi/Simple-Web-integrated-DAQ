#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
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
  Double_t timestamp = std::numeric_limits<Double_t>::quiet_NaN();
};

struct Kc705TofChannelDef {
  uint8_t channel_id = 0;
  const char* name = "";
  const char* summary_name = "";
};

inline constexpr std::array<Kc705TofChannelDef, 9> kKc705TofChannelDefs = {{
    {3,  "RECBE 1",   "recbe18"},
    {2,  "RECBE 2",   "recbe19"},
    {9,  "RECBE 3",   "recbe20"},
    {8,  "MKii v1 1", "mkii1"},
    {6,  "MKii v1 2", "mkii2"},
    {12, "MKii v2",   "mkii3"},
    {4,  "ROESTI 1",  "roesti21"},
    {5,  "ROESTI 2",  "roesti22"},
    {10, "ROESTI 3",  "roesti23"},
}};

inline constexpr std::size_t Kc705TofNamedChannelCount() {
  return kKc705TofChannelDefs.size();
}

inline constexpr const Kc705TofChannelDef* FindKc705TofChannelDef(uint8_t channel_id) {
  for (const auto& channel_def : kKc705TofChannelDefs) {
    if (channel_def.channel_id == channel_id) {
      return &channel_def;
    }
  }
  return nullptr;
}

inline constexpr const char* Kc705TofChannelName(uint8_t channel_id) {
  const auto* channel_def = FindKc705TofChannelDef(channel_id);
  return channel_def != nullptr ? channel_def->name : "Unknown";
}

inline constexpr std::optional<std::size_t> Kc705TofChannelSortIndex(uint8_t channel_id) {
  for (std::size_t i = 0; i < kKc705TofChannelDefs.size(); ++i) {
    if (kKc705TofChannelDefs[i].channel_id == channel_id) {
      return i;
    }
  }
  return std::nullopt;
}

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
