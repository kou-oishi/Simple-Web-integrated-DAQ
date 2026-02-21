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

  bool DecodeFrame(const std::vector<uint8_t>& frame, DecodedMessage& out_message, std::string& error_text) override;
  std::vector<TreeBranchDef> TreeBranches() const override;
  bool DecodedToTreeValues(const DecodedMessage& message,
                              std::vector<TreeValue>& out_values,
                              std::string& error_text) const override;
  bool FormatDecoded(const DecodedMessage& message, std::string& out_text, std::string& error_text) const override;

 private:
  static bool frame_to_event(const std::vector<uint8_t>& frame, Kc705TofEvent& out_event, std::string& error_text);
};

class Kc705TofDecoderFactory final : public IMonitorDecoderFactory {
 public:
  const char* Name() const override;
  bool Create(const std::string& spec,
              std::unique_ptr<IDecoder>& out_decoder,
              std::size_t& out_frame_size,
              std::string& error_text) const override;
};

const IMonitorDecoderFactory& GetKc705TofDecoderFactory();
