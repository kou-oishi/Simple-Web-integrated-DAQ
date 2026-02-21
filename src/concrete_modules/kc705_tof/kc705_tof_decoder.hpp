#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "concrete_modules/kc705_tof/kc705_tof_types.hpp"
#include "monitor/decoder.hpp"
#include "concrete_modules/module_registry.hpp"

class Kc705TofDecoder final : public IDecoder {
 public:
  Kc705TofDecoder() = default;

  bool decode_frame(const std::vector<uint8_t>& frame, DecodedMessage& out_message, std::string& error_text) override;
  std::vector<TreeBranchDef> tree_branches() const override;
  bool decoded_to_tree_values(const DecodedMessage& message,
                              std::vector<TreeValue>& out_values,
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

const IMonitorDecoderFactory& GetKc705TofDecoderFactory();
