#include "monitor/sink.hpp"

#include <iostream>

TextSink::TextSink(IDecoder* decoder, std::string output_path, uint64_t print_every, bool print_summary)
    : decoder_(decoder),
      output_path_(std::move(output_path)),
      use_stdout_(output_path_.empty() || output_path_ == "-"),
      print_every_(print_every == 0 ? 1 : print_every),
      print_summary_(print_summary) {}

bool TextSink::EnsureOpen(std::string& error_text) {
  error_text.clear();
  if (decoder_ == nullptr) {
    error_text = "text sink has null decoder";
    return false;
  }
  if (use_stdout_) {
    return true;
  }
  if (ofs_.is_open()) {
    return true;
  }

  ofs_.open(output_path_, std::ios::out | std::ios::trunc);
  if (!ofs_.is_open()) {
    error_text = "failed to open text output file: " + output_path_;
    return false;
  }
  return true;
}

bool TextSink::Consume(const DecodedMessage& message, std::string& error_text) {
  if (!EnsureOpen(error_text)) {
    return false;
  }

  ++seen_;
  if (seen_ % print_every_ != 0) {
    return true;
  }

  std::string text;
  bool quiet = false;
  if (!decoder_->FormatDecoded(message, text, quiet, error_text)) {
    return false;
  }
  if (quiet) {
    return true;
  }

  if (use_stdout_) {
    std::cout << "run=" << message.run_number << " subrun=" << message.subrun_number
              << " event=" << message.event_number;
    if (!text.empty()) {
      std::cout << " " << text;
    }
    std::cout << "\n";
    std::cout.flush();
    return true;
  }

  ofs_ << "run=" << message.run_number << " subrun=" << message.subrun_number
       << " event=" << message.event_number;
  if (!text.empty()) {
    ofs_ << " " << text;
  }
  ofs_ << "\n";
  ofs_.flush();
  if (!ofs_.good()) {
    error_text = "failed to write text output";
    return false;
  }
  return true;
}

bool TextSink::Finalise(std::string& error_text) {
  error_text.clear();
  if (use_stdout_ && print_summary_) {
    std::cout << "processed_events=" << seen_ << "\n";
  }
  if (ofs_.is_open()) {
    ofs_.flush();
    ofs_.close();
  }
  return true;
}
