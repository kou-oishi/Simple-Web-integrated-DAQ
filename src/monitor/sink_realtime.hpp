#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "monitor/realtime_analysis.hpp"
#include "monitor/sink.hpp"

class TApplication;
class TCanvas;
class TObject;
class TVirtualPad;

class RealtimeAnalysisSink : public IEventSink {
 public:
  explicit RealtimeAnalysisSink(std::vector<std::unique_ptr<IRealtimeAnalysis>> analyses,
                                uint32_t gui_update_interval_ms = 50);
  ~RealtimeAnalysisSink() override;

  bool Consume(const DecodedMessage& message, std::string& error_text) override;
  bool Finalise(std::string& error_text) override;

  // Called through IRealtimeDisplayRegistry implementation in sink_realtime.cpp.
  bool RegisterCanvas(TCanvas* canvas, std::string& error_text);
  bool RegisterDrawable(TVirtualPad* pad, TObject* object, const char* draw_option, std::string& error_text);

 private:
  struct DrawableBinding {
    TVirtualPad* pad = nullptr;
    TObject* object = nullptr;
    std::string draw_option;
  };

  bool EnsureInitialised(std::string& error_text);
  void RedrawAll();
  void PumpGui();

  std::vector<std::unique_ptr<IRealtimeAnalysis>> analyses_;
  bool initialised_ = false;
  bool finalised_ = false;
  uint32_t gui_update_interval_ms_ = 50;
  std::chrono::steady_clock::time_point last_gui_update_;

  TApplication* app_ = nullptr;
  std::vector<TCanvas*> canvases_;
  std::vector<DrawableBinding> drawables_;
};
