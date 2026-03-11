#include "monitor/sink_realtime.hpp"

#include <filesystem>
#include <thread>
#include <utility>

#include <zmq.h>

#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
#include <TApplication.h>
#include <TCanvas.h>
#include <TError.h>
#include <TGraph.h>
#include <TObject.h>
#include <TROOT.h>
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
    : RealtimeAnalysisSink(std::move(analyses), {}, Options{gui_update_interval_ms, true, false, nullptr, "", 1000, ""}) {}

RealtimeAnalysisSink::RealtimeAnalysisSink(std::vector<std::unique_ptr<IRealtimeAnalysis>> analyses,
                                           std::vector<std::string> analysis_names,
                                           Options options)
    : analyses_(std::move(analyses)),
      analysis_names_(std::move(analysis_names)),
      enable_gui_(options.enable_gui),
      redraw_only_on_finalise_(options.redraw_only_on_finalise),
      stop_requested_(options.stop_requested),
      gui_update_interval_ms_(options.gui_update_interval_ms == 0 ? 1 : options.gui_update_interval_ms),
      snapshot_dir_(std::move(options.snapshot_dir)),
      snapshot_interval_ms_(options.snapshot_interval_ms == 0 ? 1 : options.snapshot_interval_ms),
      snapshot_select_endpoint_(std::move(options.snapshot_select_endpoint)),
      last_gui_update_(std::chrono::steady_clock::now()),
      last_snapshot_update_(std::chrono::steady_clock::now()) {
  if (analysis_names_.size() < analyses_.size()) {
    analysis_names_.resize(analyses_.size());
  }
}

RealtimeAnalysisSink::~RealtimeAnalysisSink() {
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
  // Destroy analysis-owned ROOT objects first, then tear down TApplication.
  analyses_.clear();
  drawables_.clear();
  canvases_.clear();
  if (snapshot_select_rep_ != nullptr) {
    zmq_close(snapshot_select_rep_);
    snapshot_select_rep_ = nullptr;
  }
  if (snapshot_select_ctx_ != nullptr) {
    zmq_ctx_term(snapshot_select_ctx_);
    snapshot_select_ctx_ = nullptr;
  }
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
  canvas_analysis_names_.push_back(current_analysis_name_);
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
  if (!InitialiseSelectedAnalysisControl(error_text)) {
    return false;
  }
  if (!enable_gui_) {
    gROOT->SetBatch(kTRUE);
  } else if (gApplication == nullptr) {
    static int argc = 1;
    static char arg0[] = "datamon";
    static char* argv[] = {arg0, nullptr};
    app_ = new TApplication("datamon_realtime", &argc, argv);
  }

  RootDisplayRegistry registry(*this);
  for (std::size_t i = 0; i < analyses_.size(); ++i) {
    auto& analysis = analyses_[i];
    current_analysis_name_ = analysis_names_[i];
    analysis->SetDisplayRegistry(&registry);
    if (!analysis->Initialise(error_text)) {
      return false;
    }
  }
  current_analysis_name_.clear();

  if (!redraw_only_on_finalise_) {
    if (!RedrawAll(error_text)) {
      return false;
    }
    if (enable_gui_) {
      gSystem->ProcessEvents();
    }
    SaveSnapshots();
  }
  initialised_ = true;
  return true;
#else
  error_text = "realtime analysis requires ROOT-enabled build";
  return false;
#endif
}

bool RealtimeAnalysisSink::BeginRun(uint32_t run_number, std::string& error_text) {
  error_text.clear();
  for (auto& analysis : analyses_) {
    if (!analysis->BeginOfRun(run_number, error_text)) {
      return false;
    }
  }
  has_active_run_ = true;
  active_run_number_ = run_number;
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
  if (!redraw_only_on_finalise_) {
    // Force one immediate refresh at run start so UI snapshots reflect BeginOfRun state
    // without waiting for the periodic snapshot timer.
    if (!RedrawAll(error_text)) {
      return false;
    }
    if (enable_gui_) {
      gSystem->ProcessEvents();
    }
    SaveSnapshots();
  }
  const auto now = std::chrono::steady_clock::now();
  last_gui_update_ = now;
  last_snapshot_update_ = now;
#endif
  return true;
}

bool RealtimeAnalysisSink::EndRun(uint32_t run_number, std::string& error_text) {
  error_text.clear();
  for (auto& analysis : analyses_) {
    if (!analysis->EndOfRun(run_number, error_text)) {
      return false;
    }
  }
  return true;
}

bool RealtimeAnalysisSink::UpdateAnalysesForDraw(std::string& error_text) {
  error_text.clear();
  for (auto& analysis : analyses_) {
    if (!analysis->UpdateDrawables(error_text)) {
      return false;
    }
  }
  return true;
}

bool RealtimeAnalysisSink::RedrawAll(std::string& error_text) {
  error_text.clear();
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
  if (!UpdateAnalysesForDraw(error_text)) {
    return false;
  }
  for (const auto& binding : drawables_) {
    const auto* graph = dynamic_cast<const TGraph*>(binding.object);
    if (graph != nullptr && graph->GetN() <= 0) {
      continue;
    }
    binding.pad->cd();
    const Int_t previous_error_level = gErrorIgnoreLevel;
    gErrorIgnoreLevel = kWarning;
    binding.object->Draw(binding.draw_option.c_str());
    binding.pad->Modified();
    binding.pad->Update();
    gErrorIgnoreLevel = previous_error_level;
  }
#endif
  return true;
}

bool RealtimeAnalysisSink::PumpGui(std::string& error_text) {
  error_text.clear();
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
  if (redraw_only_on_finalise_) {
    return true;
  }

  const auto now = std::chrono::steady_clock::now();
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_gui_update_).count();
  const bool gui_due = elapsed_ms >= static_cast<long long>(gui_update_interval_ms_);
  const auto snapshot_elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_snapshot_update_).count();
  const bool snapshot_due = !snapshot_dir_.empty() && snapshot_elapsed_ms >= static_cast<long long>(snapshot_interval_ms_);

  if (!snapshot_dir_.empty()) {
    PollSelectedAnalysisControl();
  }
  const bool snapshot_selected = !selected_analysis_name_.empty();
  const bool redraw_due = (enable_gui_ && gui_due) || (snapshot_due && snapshot_selected);

  if (redraw_due && !RedrawAll(error_text)) {
    return false;
  }
  if (gui_due) {
    last_gui_update_ = now;
  }
  if (enable_gui_) {
    gSystem->ProcessEvents();
  }
  if (snapshot_due) {
    SaveSnapshots();
    last_snapshot_update_ = now;
  }
#endif
  return true;
}

std::string RealtimeAnalysisSink::SanitisePathPart(const std::string& text) const {
  std::string out;
  out.reserve(text.size());
  for (const char ch : text) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' ||
                    ch == '-' || ch == '.';
    out.push_back(ok ? ch : '_');
  }
  if (out.empty()) {
    out = "unnamed";
  }
  return out;
}

void RealtimeAnalysisSink::SaveSnapshots() {
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
  if (snapshot_dir_.empty()) {
    return;
  }
  PollSelectedAnalysisControl();
  if (selected_analysis_name_.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(snapshot_dir_, ec);
  if (ec) {
    return;
  }

  for (std::size_t i = 0; i < canvases_.size(); ++i) {
    TCanvas* canvas = canvases_[i];
    if (canvas == nullptr) {
      continue;
    }
    const std::string analysis = i < canvas_analysis_names_.size() ? canvas_analysis_names_[i] : "";
    if (analysis != selected_analysis_name_) {
      continue;
    }
    const std::string analysis_dir = SanitisePathPart(analysis.empty() ? "default" : analysis);
    const std::filesystem::path out_dir = std::filesystem::path(snapshot_dir_) / analysis_dir;
    std::filesystem::create_directories(out_dir, ec);
    if (ec) {
      continue;
    }
    const std::string canvas_name = SanitisePathPart(canvas->GetName() == nullptr ? "canvas" : canvas->GetName());
    const std::filesystem::path out_path = out_dir / (canvas_name + ".png");
    const Int_t previous_error_level = gErrorIgnoreLevel;
    gErrorIgnoreLevel = kWarning;
    canvas->SaveAs(out_path.string().c_str());
    gErrorIgnoreLevel = previous_error_level;
  }
#endif
}

bool RealtimeAnalysisSink::InitialiseSelectedAnalysisControl(std::string& error_text) {
  error_text.clear();
  if (snapshot_select_endpoint_.empty()) {
    return true;
  }
  if (snapshot_select_rep_ != nullptr) {
    return true;
  }
  snapshot_select_ctx_ = zmq_ctx_new();
  if (snapshot_select_ctx_ == nullptr) {
    error_text = "failed to create snapshot selection ZMQ context";
    return false;
  }
  snapshot_select_rep_ = zmq_socket(snapshot_select_ctx_, ZMQ_REP);
  if (snapshot_select_rep_ == nullptr) {
    error_text = "failed to create snapshot selection ZMQ REP socket";
    zmq_ctx_term(snapshot_select_ctx_);
    snapshot_select_ctx_ = nullptr;
    return false;
  }
  const int recv_timeout_ms = 0;
  zmq_setsockopt(snapshot_select_rep_, ZMQ_RCVTIMEO, &recv_timeout_ms, sizeof(recv_timeout_ms));
  if (zmq_bind(snapshot_select_rep_, snapshot_select_endpoint_.c_str()) != 0) {
    error_text = "failed to bind snapshot selection endpoint: " + snapshot_select_endpoint_;
    zmq_close(snapshot_select_rep_);
    snapshot_select_rep_ = nullptr;
    zmq_ctx_term(snapshot_select_ctx_);
    snapshot_select_ctx_ = nullptr;
    return false;
  }
  return true;
}

void RealtimeAnalysisSink::PollSelectedAnalysisControl() {
  if (snapshot_select_rep_ == nullptr) {
    return;
  }

  while (true) {
    zmq_msg_t request;
    zmq_msg_init(&request);
    const int rc = zmq_msg_recv(&request, snapshot_select_rep_, ZMQ_DONTWAIT);
    if (rc < 0) {
      zmq_msg_close(&request);
      break;
    }

    const auto* data = static_cast<const char*>(zmq_msg_data(&request));
    const auto size = static_cast<std::size_t>(zmq_msg_size(&request));
    std::string text(data, data + size);
    zmq_msg_close(&request);

    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ' || text.back() == '\t')) {
      text.pop_back();
    }
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) {
      ++begin;
    }
    if (begin > 0) {
      text.erase(0, begin);
    }
    selected_analysis_name_ = text;
    const char* reply = "ok";
    zmq_send(snapshot_select_rep_, reply, 2, 0);
  }
}

bool RealtimeAnalysisSink::Consume(const DecodedMessage& message, std::string& error_text) {
  if (!EnsureInitialised(error_text)) {
    return false;
  }

  if (!has_active_run_) {
    if (!BeginRun(message.run_number, error_text)) {
      return false;
    }
  } else if (message.run_number != active_run_number_) {
    if (!EndRun(active_run_number_, error_text)) {
      return false;
    }
    if (!BeginRun(message.run_number, error_text)) {
      return false;
    }
  }

  error_text.clear();
  for (auto& analysis : analyses_) {
    if (!analysis->Event(message, error_text)) {
      return false;
    }
  }
  return PumpGui(error_text);
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

  if (has_active_run_) {
    if (!EndRun(active_run_number_, error_text)) {
      return false;
    }
    has_active_run_ = false;
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
  if (!RedrawAll(error_text)) {
    return false;
  }
  if (enable_gui_) {
    gSystem->ProcessEvents();
  }
  SaveSnapshots();
  if (enable_gui_ && redraw_only_on_finalise_) {
    while (true) {
      if (stop_requested_ != nullptr && *stop_requested_ != 0) {
        break;
      }

      bool any_canvas_alive = false;
      for (TCanvas* canvas : canvases_) {
        if (canvas != nullptr && !canvas->IsBatch() && canvas->GetCanvasImp() != nullptr) {
          any_canvas_alive = true;
          break;
        }
      }
      if (!any_canvas_alive) {
        break;
      }

      gSystem->ProcessEvents();
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
#endif
  return true;
}
