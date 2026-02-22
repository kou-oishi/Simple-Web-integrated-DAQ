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
  static constexpr uint16_t kExpectedHeader = 0xAA55U;
  static constexpr uint16_t kExpectedFooter = 0x55AAU;
  static constexpr size_t kHeaderSize = 2;
  static constexpr size_t kPayloadSize = 8;
  static constexpr size_t kFooterSize = 2;
  static constexpr size_t kWireFrameSize = kHeaderSize + kPayloadSize + kFooterSize;
  uint8_t expected_board_id_;
};
