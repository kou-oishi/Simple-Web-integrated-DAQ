#include "monitor/pipeline.hpp"

MonitorPipeline::MonitorPipeline(std::unique_ptr<IFrameSource> source,
                                 std::unique_ptr<IDecoder> decoder,
                                 std::vector<std::unique_ptr<IEventSink>> sinks)
    : source_(std::move(source)), decoder_(std::move(decoder)), sinks_(std::move(sinks)) {}

bool MonitorPipeline::Run(uint64_t max_events,
                          std::string& error_text,
                          const volatile std::sig_atomic_t* stop_requested,
                          uint64_t* out_processed_events) {
  error_text.clear();
  if (out_processed_events != nullptr) {
    *out_processed_events = 0;
  }

  auto finalise_sinks = [&](std::string& out_error_text) -> bool {
    std::string finalise_error;
    for (auto& sink : sinks_) {
      if (!sink->Finalise(finalise_error)) {
        if (out_error_text.empty()) {
          out_error_text = finalise_error;
        } else {
          out_error_text += " | Finalise failed: " + finalise_error;
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

    FrameEnvelope frame;
    const SourceStatus st = source_->NextFrame(frame, error_text, stop_requested);
    if (st == SourceStatus::kEof) {
      break;
    }
    if (st == SourceStatus::kError) {
      (void)finalise_sinks(error_text);
      return false;
    }

    DecodedMessage message;
    message.run_number = frame.run_number;
    message.subrun_number = frame.subrun_number;
    message.event_number = frame.event_number;
    if (!decoder_->DecodeFrame(frame.payload, message, error_text)) {
      (void)finalise_sinks(error_text);
      return false;
    }

    for (auto& sink : sinks_) {
      if (!sink->Consume(message, error_text)) {
        (void)finalise_sinks(error_text);
        return false;
      }
    }

    ++idx;
  }

  if (out_processed_events != nullptr) {
    *out_processed_events = idx;
  }

  return finalise_sinks(error_text);
}
