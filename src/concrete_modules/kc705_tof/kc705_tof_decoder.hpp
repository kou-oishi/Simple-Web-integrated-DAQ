#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "concrete_modules/kc705_tof/kc705_tof_types.hpp"
#include "core/mysql_logger.hpp"
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
  bool FormatDecoded(const DecodedMessage& message,
                     std::string& out_text,
                     bool& out_quiet,
                     std::string& error_text) override;

 private:
  static bool frame_to_event(const std::vector<uint8_t>& frame, Kc705TofEvent& out_event, std::string& error_text);
  bool PrepareSubrunContext(uint32_t run_number, uint32_t subrun_number, std::string& error_text) const;
  bool PopulateTimestamp(Kc705TofEvent& event, std::string& error_text) const;
  double ComputeUnixTimestamp(const Kc705TofEvent& event) const;

  std::array<size_t, Kc705TofPeriodicChannelCount()> num_periodic_events_{};
  mutable MySqlLogger mysql_logger_;
  mutable uint32_t active_run_number_ = 0;
  mutable uint32_t active_subrun_number_ = 0;
  mutable bool subrun_context_loaded_ = false;
  mutable std::optional<double> subrun_start_unix_time_;
  mutable std::array<std::optional<double>, 8> first_periodic_time_by_board_{};
};

class Kc705TofDecoderFactory final : public IMonitorDecoderFactory {
 public:
  const char* Name() const override;
  const char* Title() const override;
  bool Create(const std::string& spec,
              std::unique_ptr<IDecoder>& out_decoder,
              std::size_t& out_frame_size,
              std::string& error_text) const override;
};

const IMonitorDecoderFactory& GetKc705TofDecoderFactory();
