#include "validators/kc705_tof_validator.hpp"

#include <string>

namespace {

uint64_t read_be_u64(const uint8_t* p) {
  uint64_t word = 0;
  for (size_t i = 0; i < 8; ++i) {
    word = (word << 8U) | static_cast<uint64_t>(p[i]);
  }
  return word;
}

}  // namespace

Kc705TofValidator::Kc705TofValidator(uint8_t expected_board_id) : expected_board_id_(expected_board_id) {}

void Kc705TofValidator::reset() { buffer_.clear(); }

ValidationResult Kc705TofValidator::feed(const uint8_t* data, size_t size, std::vector<std::vector<uint8_t>>& out_frames) {
  out_frames.clear();

  if (data == nullptr && size > 0) {
    return ValidationResult::Fatal(ValidationResult::ErrorCode::kNullInput, "null data pointer");
  }

  // Keep partial data in an internal buffer until a full 8-byte frame is available.
  buffer_.insert(buffer_.end(), data, data + size);

  while (buffer_.size() >= kFrameSize) {
    const uint64_t word = read_be_u64(buffer_.data());
    const uint8_t board_id = static_cast<uint8_t>((word >> 61U) & 0x7U);
    if (board_id != expected_board_id_) {
      return ValidationResult::Fatal(
          ValidationResult::ErrorCode::kHeaderMismatch,
          "board_id mismatch: expected " + std::to_string(expected_board_id_) + ", got " + std::to_string(board_id),
          static_cast<int64_t>(board_id),
          "board_id");
    }

    std::vector<uint8_t> frame(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(kFrameSize));

    // KC705 TOF frame: [3-bit board id][5-bit channel id][56-bit value] in big-endian wire order.
    out_frames.push_back(std::move(frame));
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(kFrameSize));
  }

  return ValidationResult::Ok();
}
