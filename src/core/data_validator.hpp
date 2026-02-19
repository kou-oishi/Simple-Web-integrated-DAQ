#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/validation_result.hpp"

class IDataValidator {
 public:
  virtual ~IDataValidator() = default;

  virtual void reset() = 0;

  // Returns detailed validation outcome (ok/recoverable/fatal).
  virtual ValidationResult feed(const uint8_t* data, size_t size, std::vector<std::vector<uint8_t>>& out_frames) = 0;

 protected:
  // Shared carry-over buffer for validators that need stream reassembly.
  std::vector<uint8_t> buffer_;
};
