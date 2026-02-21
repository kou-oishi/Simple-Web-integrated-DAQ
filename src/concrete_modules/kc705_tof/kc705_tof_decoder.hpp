#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "monitor/decoder.hpp"
#include "concrete_modules/module_registry.hpp"

struct Kc705TofEvent {
  uint64_t raw_word = 0;
  uint8_t board_id = 0;
  uint8_t channel_id = 0;
  uint64_t value56 = 0;
};

class Kc705TofDecoder final : public IDecoder {
 public:
  Kc705TofDecoder() = default;

  bool decode_frame(const std::vector<uint8_t>& frame, DecodedMessage& out_message, std::string& error_text) override;
  std::vector<std::string> tree_branch_names() const override;
  bool decoded_to_tree_values(const DecodedMessage& message,
                              std::vector<uint64_t>& out_values,
                              std::string& error_text) const override;
  bool format_decoded(const DecodedMessage& message, std::string& out_text, std::string& error_text) const override;

 private:
  static bool frame_to_event(const std::vector<uint8_t>& frame, Kc705TofEvent& out_event, std::string& error_text);
};

class Kc705TofDecoderFactory final : public IMonitorDecoderFactory {
 public:
  const char* name() const override;
  bool create(const std::string& spec,
              std::unique_ptr<IDecoder>& out_decoder,
              std::size_t& out_frame_size,
              std::string& error_text) const override;
};

bool DecodedMessageToKc705TofEvent(const DecodedMessage& message, Kc705TofEvent& out_event, std::string& error_text);

const IMonitorDecoderFactory& GetKc705TofDecoderFactory();
