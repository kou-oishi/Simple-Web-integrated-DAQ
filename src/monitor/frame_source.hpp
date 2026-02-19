#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <fstream>
#include <string>
#include <vector>

enum class SourceStatus {
  kOk,
  kEof,
  kError,
};

class IFrameSource {
 public:
  virtual ~IFrameSource() = default;
  virtual SourceStatus next_frame(std::vector<uint8_t>& out_frame, std::string& error_text) = 0;
};

class FileFrameSource : public IFrameSource {
 public:
  FileFrameSource(std::string path, std::size_t frame_size);

  SourceStatus next_frame(std::vector<uint8_t>& out_frame, std::string& error_text) override;

 private:
  std::string path_;
  std::size_t frame_size_;
  bool opened_ = false;
  bool eof_ = false;
  std::ifstream ifs_;
};

class LiveRunFileSource : public IFrameSource {
 public:
  LiveRunFileSource(std::string output_dir,
                    std::size_t frame_size,
                    uint32_t run_start,
                    uint32_t poll_ms,
                    uint32_t idle_timeout_sec);

  SourceStatus next_frame(std::vector<uint8_t>& out_frame, std::string& error_text) override;

 private:
  bool open_current_file(std::string& error_text);
  bool try_advance_next_run(std::string& error_text);
  std::string run_path(uint32_t run_number) const;

  std::string output_dir_;
  std::size_t frame_size_;
  uint32_t current_run_;
  uint32_t poll_ms_;
  uint32_t idle_timeout_sec_;
  std::optional<std::ifstream> ifs_;
};
