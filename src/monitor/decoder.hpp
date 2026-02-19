#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "monitor/decoded_message.hpp"

class IDecoder {
 public:
  virtual ~IDecoder() = default;

  // 1) Convert binary frame to readable decoded representation.
  virtual bool decode_frame(const std::vector<uint8_t>& frame,
                            DecodedMessage& out_message,
                            std::string& error_text) = 0;

  // 2) Define TTree branch layout expected for this decoder.
  virtual std::vector<std::string> tree_branch_names() const = 0;

  // 3) Convert decoded message to branch values in tree_branch_names() order.
  virtual bool decoded_to_tree_values(const DecodedMessage& message,
                                      std::vector<uint64_t>& out_values,
                                      std::string& error_text) const = 0;

  // Helper for generic console output.
  virtual bool format_decoded(const DecodedMessage& message, std::string& out_text, std::string& error_text) const = 0;
};
