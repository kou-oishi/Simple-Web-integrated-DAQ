#pragma once

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

  bool run(uint64_t max_events, std::string& error_text);

 private:
  std::unique_ptr<IFrameSource> source_;
  std::unique_ptr<IDecoder> decoder_;
  std::vector<std::unique_ptr<IEventSink>> sinks_;
};
