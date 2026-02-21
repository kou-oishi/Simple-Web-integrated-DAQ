#include "monitor/sink_root.hpp"

#include <utility>

#include <TFile.h>
#include <TTree.h>

RootTreeSink::RootTreeSink(const IDecoder* decoder, std::string output_path)
    : decoder_(decoder), output_path_(std::move(output_path)) {}

RootTreeSink::~RootTreeSink() {
  std::string ignored_error;
  finalize(ignored_error);
}

bool RootTreeSink::ensure_open(std::string& error_text) {
  error_text.clear();
  if (decoder_ == nullptr) {
    error_text = "ROOT sink has null decoder";
    return false;
  }
  if (file_ != nullptr) {
    return true;
  }

  branch_defs_ = decoder_->tree_branches();
  if (branch_defs_.empty()) {
    error_text = "decoder returned empty branch definitions";
    return false;
  }

  branch_value_positions_.assign(branch_defs_.size(), 0);
  branch_values_u64_.clear();
  branch_values_f64_.clear();
  branch_values_u64_.reserve(branch_defs_.size());
  branch_values_f64_.reserve(branch_defs_.size());

  file_ = TFile::Open(output_path_.c_str(), "RECREATE");
  if (file_ == nullptr || file_->IsZombie()) {
    error_text = "failed to create ROOT file: " + output_path_;
    if (file_ != nullptr) {
      file_->Close();
      delete file_;
      file_ = nullptr;
    }
    return false;
  }

  tree_ = new TTree("events", "Decoded monitor events");
  tree_->Branch("run_number", &run_number_);
  tree_->Branch("event_number", &event_number_);
  for (std::size_t i = 0; i < branch_defs_.size(); ++i) {
    if (branch_defs_[i].type == TreeValueType::kU64) {
      const std::size_t pos = branch_values_u64_.size();
      branch_values_u64_.push_back(0ULL);
      branch_value_positions_[i] = pos;
      tree_->Branch(branch_defs_[i].name.c_str(), &branch_values_u64_[pos]);
      continue;
    }

    const std::size_t pos = branch_values_f64_.size();
    branch_values_f64_.push_back(0.0);
    branch_value_positions_[i] = pos;
    tree_->Branch(branch_defs_[i].name.c_str(), &branch_values_f64_[pos]);
  }
  return true;
}

bool RootTreeSink::consume(const DecodedMessage& message, std::string& error_text) {
  if (!ensure_open(error_text)) {
    return false;
  }

  std::vector<TreeValue> values;
  if (!decoder_->decoded_to_tree_values(message, values, error_text)) {
    return false;
  }
  if (values.size() != branch_defs_.size()) {
    error_text = "decoder returned tree value count that does not match branch definitions";
    return false;
  }

  run_number_ = static_cast<unsigned int>(message.run_number);
  event_number_ = static_cast<unsigned long long>(message.event_number);
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (values[i].type != branch_defs_[i].type) {
      error_text = "decoder returned tree value type that does not match branch definition";
      return false;
    }

    if (values[i].type == TreeValueType::kU64) {
      branch_values_u64_[branch_value_positions_[i]] = static_cast<unsigned long long>(values[i].u64);
    } else {
      branch_values_f64_[branch_value_positions_[i]] = static_cast<double>(values[i].f64);
    }
  }
  tree_->Fill();
  return true;
}

bool RootTreeSink::finalize(std::string& error_text) {
  error_text.clear();
  if (finalised_) {
    return true;
  }
  finalised_ = true;

  if (file_ == nullptr) {
    if (!ensure_open(error_text)) {
      return false;
    }
  }

  file_->cd();
  tree_->Write("", TObject::kOverwrite);
  file_->Write("", TObject::kOverwrite);
  file_->Close();
  delete file_;
  file_ = nullptr;
  tree_ = nullptr;
  return true;
}
