#pragma once

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

#include "monitor/decoder.hpp"
#include "monitor/decoded_message.hpp"

class IEventSink {
 public:
  virtual ~IEventSink() = default;
  virtual bool consume(const DecodedMessage& message, std::string& error_text) = 0;
  virtual bool finalize(std::string& error_text) = 0;
};

class TextSink : public IEventSink {
 public:
  TextSink(const IDecoder* decoder, std::string output_path, uint64_t print_every, bool print_summary);

  bool consume(const DecodedMessage& message, std::string& error_text) override;
  bool finalize(std::string& error_text) override;

 private:
  bool ensure_open(std::string& error_text);

  const IDecoder* decoder_;
  std::string output_path_;
  std::ofstream ofs_;
  bool use_stdout_ = true;
  uint64_t print_every_;
  uint64_t seen_ = 0;
  bool print_summary_ = true;
};
