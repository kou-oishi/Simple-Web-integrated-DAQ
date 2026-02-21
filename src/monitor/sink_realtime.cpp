#include "monitor/sink_realtime.hpp"

#include <utility>

#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
#include <TApplication.h>
#include <TCanvas.h>
#include <TGraph.h>
#include <TObject.h>
#include <TSystem.h>
#include <TVirtualPad.h>

namespace {

class RootDisplayRegistry final : public IRealtimeDisplayRegistry {
 public:
  explicit RootDisplayRegistry(RealtimeAnalysisSink& owner) : owner_(owner) {}

  bool RegisterCanvas(TCanvas* canvas, std::string& error_text) override {
    return owner_.RegisterCanvas(canvas, error_text);
  }

  bool RegisterDrawable(TVirtualPad* pad, TObject* object, const char* draw_option, std::string& error_text) override {
    return owner_.RegisterDrawable(pad, object, draw_option, error_text);
  }

 private:
  RealtimeAnalysisSink& owner_;
};

}  // namespace
#endif

RealtimeAnalysisSink::RealtimeAnalysisSink(std::vector<std::unique_ptr<IRealtimeAnalysis>> analyses,
                                           uint32_t gui_update_interval_ms)
    : analyses_(std::move(analyses)),
      gui_update_interval_ms_(gui_update_interval_ms == 0 ? 1 : gui_update_interval_ms),
      last_gui_update_(std::chrono::steady_clock::now()) {}

RealtimeAnalysisSink::~RealtimeAnalysisSink() {
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
  // Destroy analysis-owned ROOT objects first, then tear down TApplication.
  analyses_.clear();
  drawables_.clear();
  canvases_.clear();
  delete app_;
  app_ = nullptr;
#endif
}

bool RealtimeAnalysisSink::RegisterCanvas(TCanvas* canvas, std::string& error_text) {
  error_text.clear();
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
  if (canvas == nullptr) {
    error_text = "analysis registered null canvas";
    return false;
  }
  canvases_.push_back(canvas);
  return true;
#else
  static_cast<void>(canvas);
  error_text = "realtime analysis requires ROOT-enabled build";
  return false;
#endif
}

bool RealtimeAnalysisSink::RegisterDrawable(TVirtualPad* pad,
                                             TObject* object,
                                             const char* draw_option,
                                             std::string& error_text) {
  error_text.clear();
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
  if (pad == nullptr || object == nullptr) {
    error_text = "analysis registered null drawable binding";
    return false;
  }
  drawables_.push_back({pad, object, draw_option == nullptr ? "" : draw_option});
  return true;
#else
  static_cast<void>(pad);
  static_cast<void>(object);
  static_cast<void>(draw_option);
  error_text = "realtime analysis requires ROOT-enabled build";
  return false;
#endif
}

bool RealtimeAnalysisSink::EnsureInitialised(std::string& error_text) {
  error_text.clear();
  if (initialised_) {
    return true;
  }

#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
  if (gApplication == nullptr) {
    static int argc = 1;
    static char arg0[] = "datamon";
    static char* argv[] = {arg0, nullptr};
    app_ = new TApplication("datamon_realtime", &argc, argv);
  }

  RootDisplayRegistry registry(*this);
  for (auto& analysis : analyses_) {
    analysis->SetDisplayRegistry(&registry);
    if (!analysis->Initialise(error_text)) {
      return false;
    }
  }

  RedrawAll();
  gSystem->ProcessEvents();
  initialised_ = true;
  return true;
#else
  error_text = "realtime analysis requires ROOT-enabled build";
  return false;
#endif
}

void RealtimeAnalysisSink::RedrawAll() {
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
  for (const auto& binding : drawables_) {
    const auto* graph = dynamic_cast<const TGraph*>(binding.object);
    if (graph != nullptr && graph->GetN() <= 0) {
      continue;
    }
    binding.pad->cd();
    binding.object->Draw(binding.draw_option.c_str());
    binding.pad->Modified();
    binding.pad->Update();
  }
#endif
}

void RealtimeAnalysisSink::PumpGui() {
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_gui_update_).count();
  if (elapsed_ms >= static_cast<long long>(gui_update_interval_ms_)) {
    RedrawAll();
    last_gui_update_ = now;
  }
  gSystem->ProcessEvents();
#endif
}

bool RealtimeAnalysisSink::Consume(const DecodedMessage& message, std::string& error_text) {
  if (!EnsureInitialised(error_text)) {
    return false;
  }

  error_text.clear();
  for (auto& analysis : analyses_) {
    if (!analysis->Event(message, error_text)) {
      return false;
    }
  }
  PumpGui();
  return true;
}

bool RealtimeAnalysisSink::Finalise(std::string& error_text) {
  error_text.clear();
  if (finalised_) {
    return true;
  }
  finalised_ = true;

  if (!EnsureInitialised(error_text)) {
    return false;
  }

  for (auto& analysis : analyses_) {
    std::string local_error;
    if (!analysis->Finalise(local_error)) {
      if (error_text.empty()) {
        error_text = local_error;
      } else {
        error_text += " | analysis Finalise failed: " + local_error;
      }
      return false;
    }
  }

#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
  RedrawAll();
  gSystem->ProcessEvents();
#endif
  return true;
}
