#pragma once

#include <csignal>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "monitor/decoder.hpp"
#include "monitor/frame_source.hpp"
#include "monitor/sink.hpp"

class MonitorPipeline {
 public:
  MonitorPipeline(std::unique_ptr<IFrameSource> source,
                  std::unique_ptr<IDecoder> decoder,
                  std::vector<std::unique_ptr<IEventSink>> sinks);

  bool Run(uint64_t max_events, std::string& error_text, const volatile std::sig_atomic_t* stop_requested = nullptr);

 private:
  std::unique_ptr<IFrameSource> source_;
  std::unique_ptr<IDecoder> decoder_;
  std::vector<std::unique_ptr<IEventSink>> sinks_;
};
