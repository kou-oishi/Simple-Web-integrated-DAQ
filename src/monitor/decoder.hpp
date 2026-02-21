#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "monitor/decoded_message.hpp"

enum class TreeValueType {
  kU64,
  kF64,
};

struct TreeBranchDef {
  std::string name;
  TreeValueType type = TreeValueType::kU64;
};

struct TreeValue {
  TreeValueType type = TreeValueType::kU64;
  uint64_t u64 = 0;
  double f64 = 0.0;

  static TreeValue FromU64(uint64_t value) {
    TreeValue out;
    out.type = TreeValueType::kU64;
    out.u64 = value;
    return out;
  }

  static TreeValue FromF64(double value) {
    TreeValue out;
    out.type = TreeValueType::kF64;
    out.f64 = value;
    return out;
  }
};

class IDecoder {
 public:
  virtual ~IDecoder() = default;

  // 1) Convert binary frame to readable decoded representation.
  virtual bool DecodeFrame(const std::vector<uint8_t>& frame,
                            DecodedMessage& out_message,
                            std::string& error_text) = 0;

  // 2) Define TTree branch layout and value types expected for this decoder.
  virtual std::vector<TreeBranchDef> TreeBranches() const = 0;

  // 3) Convert decoded message to branch values in TreeBranches() order.
  virtual bool DecodedToTreeValues(const DecodedMessage& message,
                                      std::vector<TreeValue>& out_values,
                                      std::string& error_text) const = 0;

  // Helper for generic console output.
  virtual bool FormatDecoded(const DecodedMessage& message, std::string& out_text, std::string& error_text) const = 0;
};
