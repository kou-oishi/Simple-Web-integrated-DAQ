#include "monitor/frame_source.hpp"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

#include "core/defaults.hpp"

FileFrameSource::FileFrameSource(std::string path, std::size_t frame_size)
    : path_(std::move(path)), frame_size_(frame_size) {}

uint32_t FileFrameSource::DetectRunNumberFromPath() const {
  const std::string filename = std::filesystem::path(path_).filename().string();
  std::size_t run_pos = filename.find("run");
  if (run_pos == std::string::npos) {
    run_pos = filename.find("Run");
  }
  if (run_pos == std::string::npos) {
    return 0;
  }
  std::size_t pos = run_pos + 3;
  uint32_t value = 0;
  bool saw_digit = false;
  while (pos < filename.size() && std::isdigit(static_cast<unsigned char>(filename[pos])) != 0) {
    saw_digit = true;
    value = static_cast<uint32_t>(value * 10U + static_cast<uint32_t>(filename[pos] - '0'));
    ++pos;
  }
  return saw_digit ? value : 0;
}

uint32_t FileFrameSource::DetectSubrunNumberFromPath() const {
  const std::string filename = std::filesystem::path(path_).filename().string();
  const std::size_t sub_pos = filename.find("_sub");
  if (sub_pos == std::string::npos) {
    return 0;
  }
  std::size_t pos = sub_pos + 4;
  uint32_t value = 0;
  bool saw_digit = false;
  while (pos < filename.size() && std::isdigit(static_cast<unsigned char>(filename[pos])) != 0) {
    saw_digit = true;
    value = static_cast<uint32_t>(value * 10U + static_cast<uint32_t>(filename[pos] - '0'));
    ++pos;
  }
  return saw_digit ? value : 0;
}

SourceStatus FileFrameSource::NextFrame(FrameEnvelope& out_frame,
                                         std::string& error_text,
                                         const volatile std::sig_atomic_t* stop_requested) {
  out_frame.payload.clear();
  error_text.clear();
  if (stop_requested != nullptr && *stop_requested != 0) {
    return SourceStatus::kEof;
  }

  if (frame_size_ == 0) {
    error_text = "frame size must be > 0";
    return SourceStatus::kError;
  }

  if (!opened_) {
    ifs_.open(path_, std::ios::binary);
    if (!ifs_.is_open()) {
      error_text = "failed to open input file: " + path_;
      return SourceStatus::kError;
    }
    run_number_ = DetectRunNumberFromPath();
    subrun_number_ = DetectSubrunNumberFromPath();
    next_event_number_ = 0;
    opened_ = true;
  }

  if (eof_) {
    return SourceStatus::kEof;
  }

  out_frame.payload.resize(frame_size_);
  ifs_.read(reinterpret_cast<char*>(out_frame.payload.data()), static_cast<std::streamsize>(frame_size_));

  const std::streamsize n = ifs_.gcount();
  if (n == 0) {
    eof_ = true;
    out_frame.payload.clear();
    return SourceStatus::kEof;
  }
  if (n != static_cast<std::streamsize>(frame_size_)) {
    error_text = "truncated frame at end of file";
    out_frame.payload.clear();
    return SourceStatus::kError;
  }

  out_frame.run_number = run_number_;
  out_frame.subrun_number = subrun_number_;
  out_frame.event_number = next_event_number_++;

  return SourceStatus::kOk;
}

MultiFileFrameSource::MultiFileFrameSource(std::vector<std::string> paths, std::size_t frame_size)
    : paths_(std::move(paths)), frame_size_(frame_size) {}

SourceStatus MultiFileFrameSource::NextFrame(FrameEnvelope& out_frame,
                                             std::string& error_text,
                                             const volatile std::sig_atomic_t* stop_requested) {
  out_frame.payload.clear();
  error_text.clear();
  if (stop_requested != nullptr && *stop_requested != 0) {
    return SourceStatus::kEof;
  }

  while (current_index_ < paths_.size()) {
    if (current_source_ == nullptr) {
      current_source_ = std::make_unique<FileFrameSource>(paths_[current_index_], frame_size_);
    }

    std::string local_error;
    const SourceStatus st = current_source_->NextFrame(out_frame, local_error, stop_requested);
    if (st == SourceStatus::kOk) {
      return SourceStatus::kOk;
    }
    if (st == SourceStatus::kError) {
      error_text = "failed while reading '" + paths_[current_index_] + "': " + local_error;
      return SourceStatus::kError;
    }

    ++current_index_;
    current_source_.reset();
  }

  return SourceStatus::kEof;
}

LiveRunFileSource::LiveRunFileSource(std::string output_dir,
                                     std::size_t frame_size,
                                     uint32_t run_start,
                                     uint32_t poll_ms,
                                     uint32_t idle_timeout_sec)
    : output_dir_(std::move(output_dir)),
      frame_size_(frame_size),
      current_run_(run_start),
      poll_ms_(poll_ms == 0 ? 100 : poll_ms),
      idle_timeout_sec_(idle_timeout_sec) {}

std::string LiveRunFileSource::run_path(uint32_t run_number, uint32_t subrun_number) const {
  std::ostringstream oss;
  oss << output_dir_ << "/run" << std::setw(daq_defaults::kRunNumberWidth) << std::setfill('0') << run_number
      << "_sub" << std::setw(daq_defaults::kRunNumberWidth) << std::setfill('0') << subrun_number
      << ".dat";
  return oss.str();
}

bool LiveRunFileSource::OpenCurrentFile(std::string& error_text) {
  if (ifs_.has_value() && ifs_->is_open()) {
    return true;
  }

  const std::string path = run_path(current_run_, current_subrun_);
  if (!std::filesystem::exists(path)) {
    error_text = "waiting for run/subrun file: " + path;
    return false;
  }

  ifs_.emplace();
  ifs_->open(path, std::ios::binary);
  if (!ifs_->is_open()) {
    error_text = "failed to open run/subrun file: " + path;
    ifs_.reset();
    return false;
  }

  return true;
}

bool LiveRunFileSource::TryAdvanceNextRun(std::string& error_text) {
  const uint32_t next_subrun = current_subrun_ + 1;
  const std::string next_subrun_path = run_path(current_run_, next_subrun);
  if (std::filesystem::exists(next_subrun_path)) {
    if (ifs_.has_value() && ifs_->is_open()) {
      ifs_->close();
    }
    ifs_.reset();
    current_subrun_ = next_subrun;
    return OpenCurrentFile(error_text);
  }

  const uint32_t next_run = current_run_ + 1;
  const std::string next_run_path = run_path(next_run, 0);
  if (!std::filesystem::exists(next_run_path)) {
    error_text = "waiting for next run/subrun file: " + next_subrun_path + " or " + next_run_path;
    return false;
  }

  if (ifs_.has_value() && ifs_->is_open()) {
    ifs_->close();
  }
  ifs_.reset();
  current_run_ = next_run;
  current_subrun_ = 0;
  next_event_number_ = 0;
  return OpenCurrentFile(error_text);
}

SourceStatus LiveRunFileSource::NextFrame(FrameEnvelope& out_frame,
                                           std::string& error_text,
                                           const volatile std::sig_atomic_t* stop_requested) {
  out_frame.payload.clear();
  error_text.clear();
  if (stop_requested != nullptr && *stop_requested != 0) {
    return SourceStatus::kEof;
  }

  if (frame_size_ == 0) {
    error_text = "frame size must be > 0";
    return SourceStatus::kError;
  }

  const auto wait_begin = std::chrono::steady_clock::now();
  auto is_timed_out = [&]() {
    if (idle_timeout_sec_ == 0) {
      return false;
    }
    return (std::chrono::steady_clock::now() - wait_begin) >= std::chrono::seconds(idle_timeout_sec_);
  };

  while (true) {
    if (stop_requested != nullptr && *stop_requested != 0) {
      return SourceStatus::kEof;
    }

    if (!OpenCurrentFile(error_text)) {
      if (is_timed_out()) {
        return SourceStatus::kEof;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms_));
      continue;
    }

    out_frame.payload.resize(frame_size_);
    ifs_->read(reinterpret_cast<char*>(out_frame.payload.data()), static_cast<std::streamsize>(frame_size_));
    const std::streamsize n = ifs_->gcount();
    if (n == static_cast<std::streamsize>(frame_size_)) {
      out_frame.run_number = current_run_;
      out_frame.subrun_number = current_subrun_;
      out_frame.event_number = next_event_number_++;
      return SourceStatus::kOk;
    }

    ifs_->clear();
    out_frame.payload.clear();

    if (n > 0) {
      ifs_->seekg(-n, std::ios::cur);
    }

    if (TryAdvanceNextRun(error_text)) {
      continue;
    }

    if (is_timed_out()) {
      return SourceStatus::kEof;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms_));
  }
}
