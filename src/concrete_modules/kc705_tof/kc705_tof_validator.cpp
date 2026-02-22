#include "concrete_modules/kc705_tof/kc705_tof_validator.hpp"

#include <sstream>
#include <string>

namespace {

uint16_t read_be_u16(const uint8_t* p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8U) | static_cast<uint16_t>(p[1]));
}

uint64_t read_be_u64(const uint8_t* p) {
  uint64_t word = 0;
  for (size_t i = 0; i < 8; ++i) {
    word = (word << 8U) | static_cast<uint64_t>(p[i]);
  }
  return word;
}

std::string hex16(uint16_t value) {
  std::ostringstream oss;
  oss << std::hex << std::uppercase;
  oss.width(4);
  oss.fill('0');
  oss << static_cast<unsigned int>(value);
  return oss.str();
}

}  // namespace

Kc705TofValidator::Kc705TofValidator(uint8_t expected_board_id) : expected_board_id_(expected_board_id) {}

void Kc705TofValidator::Reset() { buffer_.clear(); }

ValidationResult Kc705TofValidator::Feed(const uint8_t* data, size_t size, std::vector<std::vector<uint8_t>>& out_frames) {
  out_frames.clear();

  if (data == nullptr && size > 0) {
    return ValidationResult::Fatal(ValidationResult::ErrorCode::kNullInput, "null data pointer");
  }

  // Keep partial data in an internal buffer until a full wire frame is available.
  if (size > 0) {
    buffer_.insert(buffer_.end(), data, data + size);
  }

  while (buffer_.size() >= kWireFrameSize) {
    const uint16_t header = read_be_u16(buffer_.data());
    if (header != kExpectedHeader) {
      return ValidationResult::Fatal(
          ValidationResult::ErrorCode::kHeaderMismatch,
          "header mismatch: expected 0xAA55, got 0x" + hex16(header),
          static_cast<int64_t>(header),
          "header");
    }

    const uint64_t word = read_be_u64(buffer_.data() + static_cast<std::ptrdiff_t>(kHeaderSize));
    const uint8_t board_id = static_cast<uint8_t>((word >> 61U) & 0x7U);
    if (board_id != expected_board_id_) {
      return ValidationResult::Fatal(
          ValidationResult::ErrorCode::kHeaderMismatch,
          "board_id mismatch: expected " + std::to_string(expected_board_id_) + ", got " + std::to_string(board_id),
          static_cast<int64_t>(board_id),
          "board_id");
    }

    const uint16_t footer = read_be_u16(buffer_.data() + static_cast<std::ptrdiff_t>(kHeaderSize + kPayloadSize));
    if (footer != kExpectedFooter) {
      return ValidationResult::Fatal(
          ValidationResult::ErrorCode::kHeaderMismatch,
          "footer mismatch: expected 0x55AA, got 0x" + hex16(footer),
          static_cast<int64_t>(footer),
          "footer");
    }

    std::vector<uint8_t> frame(buffer_.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
                               buffer_.begin() + static_cast<std::ptrdiff_t>(kHeaderSize + kPayloadSize));

    // KC705 TOF payload: [3-bit board id][5-bit channel id][56-bit value] in big-endian wire order.
    out_frames.push_back(std::move(frame));
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(kWireFrameSize));
  }

  return ValidationResult::Ok();
}
