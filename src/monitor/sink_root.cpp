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

  branch_names_ = decoder_->tree_branch_names();
  if (branch_names_.empty()) {
    error_text = "decoder returned empty branch definitions";
    return false;
  }

  branch_values_.assign(branch_names_.size(), 0ULL);

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
  tree_->Branch("event_index", &event_index_);
  for (std::size_t i = 0; i < branch_names_.size(); ++i) {
    tree_->Branch(branch_names_[i].c_str(), &branch_values_[i]);
  }
  return true;
}

bool RootTreeSink::consume(const DecodedMessage& message, std::string& error_text) {
  if (!ensure_open(error_text)) {
    return false;
  }

  std::vector<uint64_t> values;
  if (!decoder_->decoded_to_tree_values(message, values, error_text)) {
    return false;
  }
  if (values.size() != branch_values_.size()) {
    error_text = "decoder returned tree value count that does not match branch definitions";
    return false;
  }

  event_index_ = static_cast<unsigned long long>(message.event_index);
  for (std::size_t i = 0; i < values.size(); ++i) {
    branch_values_[i] = static_cast<unsigned long long>(values[i]);
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

