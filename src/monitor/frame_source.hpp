#pragma once

#include <csignal>
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

struct FrameEnvelope {
  uint32_t run_number = 0;
  uint32_t subrun_number = 0;
  uint64_t event_number = 0;
  std::vector<uint8_t> payload;
};

class IFrameSource {
 public:
  virtual ~IFrameSource() = default;
  virtual SourceStatus NextFrame(FrameEnvelope& out_frame,
                                  std::string& error_text,
                                  const volatile std::sig_atomic_t* stop_requested = nullptr) = 0;
};

class FileFrameSource : public IFrameSource {
 public:
  FileFrameSource(std::string path, std::size_t frame_size);

  SourceStatus NextFrame(FrameEnvelope& out_frame,
                          std::string& error_text,
                          const volatile std::sig_atomic_t* stop_requested = nullptr) override;

 private:
  uint32_t DetectRunNumberFromPath() const;
  uint32_t DetectSubrunNumberFromPath() const;

  std::string path_;
  std::size_t frame_size_;
  uint32_t run_number_ = 0;
  uint32_t subrun_number_ = 0;
  uint64_t next_event_number_ = 0;
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

  SourceStatus NextFrame(FrameEnvelope& out_frame,
                          std::string& error_text,
                          const volatile std::sig_atomic_t* stop_requested = nullptr) override;

 private:
  bool OpenCurrentFile(std::string& error_text);
  bool TryAdvanceNextRun(std::string& error_text);
  std::string run_path(uint32_t run_number, uint32_t subrun_number) const;

  std::string output_dir_;
  std::size_t frame_size_;
  uint32_t current_run_;
  uint32_t current_subrun_ = 0;
  uint64_t next_event_number_ = 0;
  uint32_t poll_ms_;
  uint32_t idle_timeout_sec_;
  std::optional<std::ifstream> ifs_;
};
