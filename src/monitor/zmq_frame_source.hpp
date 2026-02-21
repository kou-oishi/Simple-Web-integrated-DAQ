#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "monitor/frame_source.hpp"

class ZmqDataFrameSource : public IFrameSource {
 public:
  ZmqDataFrameSource(std::string endpoint, uint32_t poll_timeout_ms, uint32_t idle_timeout_sec);
  ~ZmqDataFrameSource() override;

  SourceStatus NextFrame(FrameEnvelope& out_frame,
                          std::string& error_text,
                          const volatile std::sig_atomic_t* stop_requested = nullptr) override;

 private:
  bool EnsureConnected(std::string& error_text);

  std::string endpoint_;
  uint32_t poll_timeout_ms_;
  uint32_t idle_timeout_sec_;

  void* ctx_ = nullptr;
  void* sub_ = nullptr;
  bool connected_ = false;
};
