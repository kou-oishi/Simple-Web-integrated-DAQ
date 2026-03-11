#include "monitor/sink.hpp"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

std::string current_timestamp_text() {
  const std::time_t now = std::time(nullptr);
  std::tm tm_value{};
#if defined(_WIN32)
  localtime_s(&tm_value, &now);
#else
  localtime_r(&now, &tm_value);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm_value, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

}  // namespace

TextSink::TextSink(IDecoder* decoder,
                   std::string output_path,
                   uint64_t print_every,
                   bool print_summary,
                   bool with_timestamp)
    : decoder_(decoder),
      output_path_(std::move(output_path)),
      use_stdout_(output_path_.empty() || output_path_ == "-"),
      print_every_(print_every == 0 ? 1 : print_every),
      print_summary_(print_summary),
      with_timestamp_(with_timestamp) {}

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

  if (message.is_historical_replay) {
    return true;
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
    if (with_timestamp_) {
      std::cout << "[" << current_timestamp_text() << "] ";
    }
    std::cout << "run=" << message.run_number << " subrun=" << message.subrun_number
              << " event=" << message.event_number;
    if (!text.empty()) {
      std::cout << " " << text;
    }
    std::cout << "\n";
    std::cout.flush();
    return true;
  }

  if (with_timestamp_) {
    ofs_ << "[" << current_timestamp_text() << "] ";
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
