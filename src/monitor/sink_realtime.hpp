#pragma once

#include <chrono>
#include <filesystem>
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
  struct Options {
    uint32_t gui_update_interval_ms = 50;
    bool enable_gui = true;
    std::string snapshot_dir;
    uint32_t snapshot_interval_ms = 1000;
    std::string snapshot_select_endpoint;
  };

  explicit RealtimeAnalysisSink(std::vector<std::unique_ptr<IRealtimeAnalysis>> analyses,
                                uint32_t gui_update_interval_ms = 50);
  explicit RealtimeAnalysisSink(std::vector<std::unique_ptr<IRealtimeAnalysis>> analyses,
                                std::vector<std::string> analysis_names,
                                Options options);
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
  void SaveSnapshots();
  void PollSelectedAnalysisControl();
  bool InitialiseSelectedAnalysisControl(std::string& error_text);
  std::string SanitisePathPart(const std::string& text) const;

  std::vector<std::unique_ptr<IRealtimeAnalysis>> analyses_;
  std::vector<std::string> analysis_names_;
  bool initialised_ = false;
  bool finalised_ = false;
  bool enable_gui_ = true;
  uint32_t gui_update_interval_ms_ = 50;
  std::string snapshot_dir_;
  uint32_t snapshot_interval_ms_ = 1000;
  std::string snapshot_select_endpoint_;
  std::string selected_analysis_name_;
  void* snapshot_select_ctx_ = nullptr;
  void* snapshot_select_rep_ = nullptr;
  std::chrono::steady_clock::time_point last_gui_update_;
  std::chrono::steady_clock::time_point last_snapshot_update_;
  std::string current_analysis_name_;

  TApplication* app_ = nullptr;
  std::vector<TCanvas*> canvases_;
  std::vector<std::string> canvas_analysis_names_;
  std::vector<DrawableBinding> drawables_;
};
