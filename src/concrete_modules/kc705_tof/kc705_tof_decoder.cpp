#include "concrete_modules/kc705_tof/kc705_tof_decoder.hpp"

#include <limits>
#include <cstdio>

namespace {

constexpr double kTimeScale = 4.0e-9;  // 4 ns per count

uint64_t read_be_u64(const uint8_t* p) {
  uint64_t word = 0;
  for (size_t i = 0; i < 8; ++i) {
    word = (word << 8U) | static_cast<uint64_t>(p[i]);
  }
  return word;
}

bool message_to_event(const DecodedMessage& message, Kc705TofEvent& out_event, std::string& error_text) {
  const auto* Event = std::any_cast<Kc705TofEvent>(&message.payload);
  if (Event == nullptr) {
    error_text = "kc705_tof decoder received unsupported decoded payload type";
    return false;
  }
  out_event = *Event;
  return true;
}

}  // namespace

bool Kc705TofDecoder::PrepareSubrunContext(uint32_t run_number,
                                           uint32_t subrun_number,
                                           std::string& error_text) const {
  error_text.clear();
  if (subrun_context_loaded_ && active_run_number_ == run_number && active_subrun_number_ == subrun_number) {
    return true;
  }

  active_run_number_ = run_number;
  active_subrun_number_ = subrun_number;
  subrun_context_loaded_ = true;
  subrun_start_unix_time_.reset();
  for (auto& first_periodic_time : first_periodic_time_by_board_) {
    first_periodic_time.reset();
  }

  double subrun_start_unix_time = 0.0;
  bool found = false;
  if (!mysql_logger_.GetSubrunStartUnixTime(run_number, subrun_number, subrun_start_unix_time, found, error_text)) {
    return false;
  }
  if (found) {
    subrun_start_unix_time_ = subrun_start_unix_time;
  }
  return true;
}

double Kc705TofDecoder::ComputeUnixTimestamp(const Kc705TofEvent& event) const {
  if (!subrun_start_unix_time_.has_value() || event.board_id >= first_periodic_time_by_board_.size()) {
    return std::numeric_limits<double>::quiet_NaN();
  }

  const auto& first_periodic_time = first_periodic_time_by_board_[event.board_id];
  if (!first_periodic_time.has_value()) {
    return std::numeric_limits<double>::quiet_NaN();
  }

  return *subrun_start_unix_time_ + (static_cast<double>(event.time) - *first_periodic_time);
}

bool Kc705TofDecoder::frame_to_event(const std::vector<uint8_t>& frame,
                                     Kc705TofEvent& out_event,
                                     std::string& error_text) {
  error_text.clear();
  if (frame.size() != 8) {
    error_text = "kc705_tof frame must be 8 bytes";
    return false;
  }

  const uint64_t word = read_be_u64(frame.data());

  out_event.raw_word = word;
  out_event.board_id = static_cast<uint8_t>((word >> 61U) & 0x7U);
  out_event.channel_id = static_cast<uint8_t>((word >> 56U) & 0x1FU);
  const uint64_t raw_time = word & 0x00FFFFFFFFFFFFFFULL;
  out_event.time = static_cast<double>(raw_time) * kTimeScale;
  return true;
}

bool Kc705TofDecoder::DecodeFrame(const std::vector<uint8_t>& frame,
                                   DecodedMessage& out_message,
                                   std::string& error_text) {
  Kc705TofEvent Event;
  if (!frame_to_event(frame, Event, error_text)) {
    return false;
  }
  Event.run_number = out_message.run_number;
  Event.subrun_number = out_message.subrun_number;
  Event.event_number = out_message.event_number;

  out_message.payload = Event;
  return true;
}

std::vector<TreeBranchDef> Kc705TofDecoder::TreeBranches() const {
  return {
      {"raw_word", TreeValueType::kU64},
      {"board_id", TreeValueType::kU64},
      {"channel_id", TreeValueType::kU64},
      {"time", TreeValueType::kF64},
      {"timestamp", TreeValueType::kF64},
  };
}

bool Kc705TofDecoder::DecodedToTreeValues(const DecodedMessage& message,
                                             std::vector<TreeValue>& out_values,
                                             std::string& error_text) const {
  Kc705TofEvent Event;
  if (!message_to_event(message, Event, error_text)) {
    return false;
  }
  if (!PrepareSubrunContext(message.run_number, message.subrun_number, error_text)) {
    return false;
  }
  if (IsPeriodicChannel(Event.channel_id) && Event.board_id < first_periodic_time_by_board_.size() &&
      !first_periodic_time_by_board_[Event.board_id].has_value()) {
    first_periodic_time_by_board_[Event.board_id] = static_cast<double>(Event.time);
  }
  const double timestamp = ComputeUnixTimestamp(Event);

  out_values.clear();
  out_values.reserve(5);
  out_values.push_back(TreeValue::FromU64(Event.raw_word));
  out_values.push_back(TreeValue::FromU64(static_cast<uint64_t>(Event.board_id)));
  out_values.push_back(TreeValue::FromU64(static_cast<uint64_t>(Event.channel_id)));
  out_values.push_back(TreeValue::FromF64(static_cast<double>(Event.time)));
  out_values.push_back(TreeValue::FromF64(timestamp));
  return true;
}

bool Kc705TofDecoder::FormatDecoded(const DecodedMessage& message,
                                    std::string& out_text,
                                    bool& out_quiet,
                                    std::string& error_text) {
  out_quiet = false;
  out_text.clear();
  Kc705TofEvent Event;
  if (!message_to_event(message, Event, error_text)) {
    return false;
  }

  char buffer[256] = {};
  std::snprintf(buffer,
                sizeof(buffer),
                "board=%u ch=%2u (%-12s) time=%.6f",
                static_cast<unsigned>(Event.board_id),
                static_cast<unsigned>(Event.channel_id),
                Kc705TofChannelName(Event.channel_id),
                static_cast<double>(Event.time));

  if (const auto periodic_index = PeriodicChannelIndex(Event.channel_id); periodic_index.has_value()) {
    const std::size_t pch_idx = *periodic_index;
    ++num_periodic_events_[pch_idx];
    if (num_periodic_events_[pch_idx] % 10000 == 1) {
      char prescaled_buffer[96] = {};
      std::snprintf(prescaled_buffer,
                    sizeof(prescaled_buffer),
                    " [prescaled: %zu th event of this channel]",
                    num_periodic_events_[pch_idx]);
      out_text = std::string(buffer) + prescaled_buffer;
    } else {
      out_quiet = true;
      return true;
    }
  } else {
    out_text = buffer;
  }
  return true;
}

const char* Kc705TofDecoderFactory::Name() const { return "kc705_tof"; }

const char* Kc705TofDecoderFactory::Title() const { return "KC705 TOF Decoder"; }

bool Kc705TofDecoderFactory::Create(const std::string& spec,
                                    std::unique_ptr<IDecoder>& out_decoder,
                                    std::size_t& out_frame_size,
                                    std::string& error_text) const {
  if (!spec.empty()) {
    error_text = "kc705_tof decoder does not accept spec; use --decoder kc705_tof";
    return false;
  }

  out_decoder = std::make_unique<Kc705TofDecoder>();
  out_frame_size = 8;
  error_text.clear();
  return true;
}

const IMonitorDecoderFactory& GetKc705TofDecoderFactory() {
  static Kc705TofDecoderFactory factory;
  return factory;
}

REGISTER_DECODER(GetKc705TofDecoderFactory);
