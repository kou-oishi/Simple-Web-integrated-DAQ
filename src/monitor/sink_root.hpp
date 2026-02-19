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
  std::vector<std::string> branch_names_;

  class TFile* file_ = nullptr;
  class TTree* tree_ = nullptr;
  bool finalised_ = false;

  unsigned long long event_index_ = 0;
  std::vector<unsigned long long> branch_values_;
};

