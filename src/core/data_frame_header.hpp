#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct DataFrameHeader {
  static constexpr uint32_t kMagic = 0x53444151U;  // 'SDAQ'
  static constexpr uint16_t kVersion = 1;
  static constexpr std::size_t kWireSize = 20;

  uint32_t run_number = 0;
  uint64_t event_number = 0;
};

inline void WriteDataFrameHeader(const DataFrameHeader& header, std::array<uint8_t, DataFrameHeader::kWireSize>& out) {
  out[0] = static_cast<uint8_t>((DataFrameHeader::kMagic >> 24U) & 0xFFU);
  out[1] = static_cast<uint8_t>((DataFrameHeader::kMagic >> 16U) & 0xFFU);
  out[2] = static_cast<uint8_t>((DataFrameHeader::kMagic >> 8U) & 0xFFU);
  out[3] = static_cast<uint8_t>(DataFrameHeader::kMagic & 0xFFU);

  out[4] = static_cast<uint8_t>((DataFrameHeader::kVersion >> 8U) & 0xFFU);
  out[5] = static_cast<uint8_t>(DataFrameHeader::kVersion & 0xFFU);

  out[6] = 0;
  out[7] = 0;

  out[8] = static_cast<uint8_t>((header.run_number >> 24U) & 0xFFU);
  out[9] = static_cast<uint8_t>((header.run_number >> 16U) & 0xFFU);
  out[10] = static_cast<uint8_t>((header.run_number >> 8U) & 0xFFU);
  out[11] = static_cast<uint8_t>(header.run_number & 0xFFU);

  for (std::size_t i = 0; i < 8; ++i) {
    out[12 + i] = static_cast<uint8_t>((header.event_number >> ((7U - i) * 8U)) & 0xFFU);
  }
}

inline bool ReadDataFrameHeader(const uint8_t* data, std::size_t size, DataFrameHeader& out) {
  if (data == nullptr || size != DataFrameHeader::kWireSize) {
    return false;
  }

  const uint32_t magic = (static_cast<uint32_t>(data[0]) << 24U) | (static_cast<uint32_t>(data[1]) << 16U) |
                         (static_cast<uint32_t>(data[2]) << 8U) | static_cast<uint32_t>(data[3]);
  if (magic != DataFrameHeader::kMagic) {
    return false;
  }

  const uint16_t version = static_cast<uint16_t>((static_cast<uint16_t>(data[4]) << 8U) | data[5]);
  if (version != DataFrameHeader::kVersion) {
    return false;
  }

  out.run_number = (static_cast<uint32_t>(data[8]) << 24U) | (static_cast<uint32_t>(data[9]) << 16U) |
                   (static_cast<uint32_t>(data[10]) << 8U) | static_cast<uint32_t>(data[11]);

  uint64_t event_number = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    event_number = (event_number << 8U) | static_cast<uint64_t>(data[12 + i]);
  }
  out.event_number = event_number;
  return true;
}
