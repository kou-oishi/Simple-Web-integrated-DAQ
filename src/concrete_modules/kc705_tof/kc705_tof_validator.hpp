#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/data_validator.hpp"

class Kc705TofValidator : public IDataValidator {
 public:
  explicit Kc705TofValidator(uint8_t expected_board_id);

  void Reset() override;
  ValidationResult Feed(const uint8_t* data, size_t size, std::vector<std::vector<uint8_t>>& out_frames) override;

 private:
  static constexpr size_t kFrameSize = 8;
  uint8_t expected_board_id_;
  std::vector<uint8_t> buffer_;
};
