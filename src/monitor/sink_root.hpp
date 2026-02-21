#pragma once

#include <string>
#include <vector>

#include "monitor/sink.hpp"

class RootTreeSink : public IEventSink {
 public:
  RootTreeSink(const IDecoder* decoder, std::string output_path);
  ~RootTreeSink() override;

  bool consume(const DecodedMessage& message, std::string& error_text) override;
  bool finalize(std::string& error_text) override;

 private:
  bool ensure_open(std::string& error_text);

  const IDecoder* decoder_;
  std::string output_path_;
  std::vector<TreeBranchDef> branch_defs_;
  std::vector<std::size_t> branch_value_positions_;

  class TFile* file_ = nullptr;
  class TTree* tree_ = nullptr;
  bool finalised_ = false;

  unsigned int run_number_ = 0;
  unsigned long long event_number_ = 0;
  std::vector<unsigned long long> branch_values_u64_;
  std::vector<double> branch_values_f64_;
};
