#include "monitor/frame_source.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <thread>

#include "core/defaults.hpp"

FileFrameSource::FileFrameSource(std::string path, std::size_t frame_size)
    : path_(std::move(path)), frame_size_(frame_size) {}

SourceStatus FileFrameSource::next_frame(std::vector<uint8_t>& out_frame,
                                         std::string& error_text,
                                         const volatile std::sig_atomic_t* stop_requested) {
  out_frame.clear();
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
    opened_ = true;
  }

  if (eof_) {
    return SourceStatus::kEof;
  }

  out_frame.resize(frame_size_);
  ifs_.read(reinterpret_cast<char*>(out_frame.data()), static_cast<std::streamsize>(frame_size_));

  const std::streamsize n = ifs_.gcount();
  if (n == 0) {
    eof_ = true;
    out_frame.clear();
    return SourceStatus::kEof;
  }
  if (n != static_cast<std::streamsize>(frame_size_)) {
    error_text = "truncated frame at end of file";
    out_frame.clear();
    return SourceStatus::kError;
  }

  return SourceStatus::kOk;
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

std::string LiveRunFileSource::run_path(uint32_t run_number) const {
  std::ostringstream oss;
  oss << output_dir_ << "/run" << std::setw(daq_defaults::kRunNumberWidth) << std::setfill('0') << run_number
      << ".dat";
  return oss.str();
}

bool LiveRunFileSource::open_current_file(std::string& error_text) {
  if (ifs_.has_value() && ifs_->is_open()) {
    return true;
  }

  const std::string path = run_path(current_run_);
  if (!std::filesystem::exists(path)) {
    error_text = "waiting for run file: " + path;
    return false;
  }

  ifs_.emplace();
  ifs_->open(path, std::ios::binary);
  if (!ifs_->is_open()) {
    error_text = "failed to open run file: " + path;
    ifs_.reset();
    return false;
  }

  return true;
}

bool LiveRunFileSource::try_advance_next_run(std::string& error_text) {
  const uint32_t next_run = current_run_ + 1;
  const std::string next_path = run_path(next_run);
  if (!std::filesystem::exists(next_path)) {
    error_text = "waiting for next run file: " + next_path;
    return false;
  }

  if (ifs_.has_value() && ifs_->is_open()) {
    ifs_->close();
  }
  ifs_.reset();
  current_run_ = next_run;
  return open_current_file(error_text);
}

SourceStatus LiveRunFileSource::next_frame(std::vector<uint8_t>& out_frame,
                                           std::string& error_text,
                                           const volatile std::sig_atomic_t* stop_requested) {
  out_frame.clear();
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

    if (!open_current_file(error_text)) {
      if (is_timed_out()) {
        return SourceStatus::kEof;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms_));
      continue;
    }

    out_frame.resize(frame_size_);
    ifs_->read(reinterpret_cast<char*>(out_frame.data()), static_cast<std::streamsize>(frame_size_));
    const std::streamsize n = ifs_->gcount();
    if (n == static_cast<std::streamsize>(frame_size_)) {
      return SourceStatus::kOk;
    }

    ifs_->clear();
    out_frame.clear();

    if (n > 0) {
      ifs_->seekg(-n, std::ios::cur);
    }

    if (try_advance_next_run(error_text)) {
      continue;
    }

    if (is_timed_out()) {
      return SourceStatus::kEof;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms_));
  }
}
