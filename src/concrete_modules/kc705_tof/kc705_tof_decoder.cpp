#include "concrete_modules/kc705_tof/kc705_tof_decoder.hpp"

#include <sstream>

namespace {

uint64_t read_be_u64(const uint8_t* p) {
  uint64_t word = 0;
  for (size_t i = 0; i < 8; ++i) {
    word = (word << 8U) | static_cast<uint64_t>(p[i]);
  }
  return word;
}

}  // namespace

bool DecodedMessageToKc705TofEvent(const DecodedMessage& message, Kc705TofEvent& out_event, std::string& error_text) {
  const auto* event = std::any_cast<Kc705TofEvent>(&message.payload);
  if (event == nullptr) {
    error_text = "kc705_tof decoder received unsupported decoded payload type";
    return false;
  }
  out_event = *event;
  return true;
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
  out_event.value56 = word & 0x00FFFFFFFFFFFFFFULL;
  return true;
}

bool Kc705TofDecoder::decode_frame(const std::vector<uint8_t>& frame,
                                   DecodedMessage& out_message,
                                   std::string& error_text) {
  Kc705TofEvent event;
  if (!frame_to_event(frame, event, error_text)) {
    return false;
  }

  out_message.payload = event;
  return true;
}

std::vector<std::string> Kc705TofDecoder::tree_branch_names() const {
  return {"raw_word", "board_id", "channel_id", "value56"};
}

bool Kc705TofDecoder::decoded_to_tree_values(const DecodedMessage& message,
                                             std::vector<uint64_t>& out_values,
                                             std::string& error_text) const {
  Kc705TofEvent event;
  if (!DecodedMessageToKc705TofEvent(message, event, error_text)) {
    return false;
  }

  out_values.clear();
  out_values.reserve(4);
  out_values.push_back(event.raw_word);
  out_values.push_back(static_cast<uint64_t>(event.board_id));
  out_values.push_back(static_cast<uint64_t>(event.channel_id));
  out_values.push_back(event.value56);
  return true;
}

bool Kc705TofDecoder::format_decoded(const DecodedMessage& message, std::string& out_text, std::string& error_text) const {
  Kc705TofEvent event;
  if (!DecodedMessageToKc705TofEvent(message, event, error_text)) {
    return false;
  }

  std::ostringstream oss;
  oss << "board=" << static_cast<unsigned>(event.board_id) << " ch=" << static_cast<unsigned>(event.channel_id)
      << " value=" << event.value56;
  out_text = oss.str();
  return true;
}

const char* Kc705TofDecoderFactory::name() const { return "kc705_tof"; }

bool Kc705TofDecoderFactory::create(const std::string& spec,
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
