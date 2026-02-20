#include "monitor/pipeline.hpp"

MonitorPipeline::MonitorPipeline(std::unique_ptr<IFrameSource> source,
                                 std::unique_ptr<IDecoder> decoder,
                                 std::vector<std::unique_ptr<IEventSink>> sinks)
    : source_(std::move(source)), decoder_(std::move(decoder)), sinks_(std::move(sinks)) {}

bool MonitorPipeline::run(uint64_t max_events,
                          std::string& error_text,
                          const volatile std::sig_atomic_t* stop_requested) {
  error_text.clear();

  auto finalize_sinks = [&](std::string& out_error_text) -> bool {
    std::string finalize_error;
    for (auto& sink : sinks_) {
      if (!sink->finalize(finalize_error)) {
        if (out_error_text.empty()) {
          out_error_text = finalize_error;
        } else {
          out_error_text += " | finalize failed: " + finalize_error;
        }
        return false;
      }
    }
    return true;
  };

  uint64_t idx = 0;
  while (max_events == 0 || idx < max_events) {
    if (stop_requested != nullptr && *stop_requested != 0) {
      break;
    }

    std::vector<uint8_t> frame;
    const SourceStatus st = source_->next_frame(frame, error_text, stop_requested);
    if (st == SourceStatus::kEof) {
      break;
    }
    if (st == SourceStatus::kError) {
      (void)finalize_sinks(error_text);
      return false;
    }

    DecodedMessage message;
    message.event_index = idx;
    if (!decoder_->decode_frame(frame, message, error_text)) {
      (void)finalize_sinks(error_text);
      return false;
    }

    for (auto& sink : sinks_) {
      if (!sink->consume(message, error_text)) {
        (void)finalize_sinks(error_text);
        return false;
      }
    }

    ++idx;
  }

  return finalize_sinks(error_text);
}
