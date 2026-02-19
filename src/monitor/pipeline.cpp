#include "monitor/pipeline.hpp"

MonitorPipeline::MonitorPipeline(std::unique_ptr<IFrameSource> source,
                                 std::unique_ptr<IDecoder> decoder,
                                 std::vector<std::unique_ptr<IEventSink>> sinks)
    : source_(std::move(source)), decoder_(std::move(decoder)), sinks_(std::move(sinks)) {}

bool MonitorPipeline::run(uint64_t max_events, std::string& error_text) {
  error_text.clear();

  uint64_t idx = 0;
  while (max_events == 0 || idx < max_events) {
    std::vector<uint8_t> frame;
    const SourceStatus st = source_->next_frame(frame, error_text);
    if (st == SourceStatus::kEof) {
      break;
    }
    if (st == SourceStatus::kError) {
      return false;
    }

    DecodedMessage message;
    message.event_index = idx;
    if (!decoder_->decode_frame(frame, message, error_text)) {
      return false;
    }

    for (auto& sink : sinks_) {
      if (!sink->consume(message, error_text)) {
        return false;
      }
    }

    ++idx;
  }

  for (auto& sink : sinks_) {
    if (!sink->finalize(error_text)) {
      return false;
    }
  }

  return true;
}
