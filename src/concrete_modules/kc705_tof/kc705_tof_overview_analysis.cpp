#include "concrete_modules/kc705_tof/kc705_tof_overview_analysis.hpp"

#include <string>

#include <TCanvas.h>
#include <TGraph.h>
#include <TH1D.h>

#include "concrete_modules/module_registry.hpp"

namespace {

bool parse_overview_spec(const ParsedAnalysisSpec& spec, std::size_t& out_trend_points, std::string& error_text) {
  if (!spec.ReadSize("trend_points", 1000, out_trend_points, error_text) || out_trend_points == 0) {
    error_text = "analysis spec trend_points must be integer > 0";
    return false;
  }

  if (!spec.RejectUnknown({"trend_points"}, error_text)) {
    return false;
  }

  return true;
}

}  // namespace

Kc705TofOverviewAnalysis::Kc705TofOverviewAnalysis(std::size_t trend_points) : trend_points_(trend_points) {}

bool Kc705TofOverviewAnalysis::Initialise(std::string& error_text) {
  error_text.clear();
  canvas_board_ = std::make_unique<TCanvas>("kc705_board_canvas", "KC705 TOF: Board ID", 900, 300);
  canvas_channel_ = std::make_unique<TCanvas>("kc705_channel_canvas", "KC705 TOF: Channel ID", 900, 300);
  canvas_trend_ = std::make_unique<TCanvas>("kc705_value_trend_canvas", "KC705 TOF: Value Trend", 900, 500);

  hist_board_ = std::make_unique<TH1D>("kc705_board_hist", "Board ID;board_id;counts", 8, -0.5, 7.5);
  hist_channel_ = std::make_unique<TH1D>("kc705_channel_hist", "Channel ID;channel_id;counts", 32, -0.5, 31.5);
  hist_board_->SetDirectory(nullptr);
  hist_channel_->SetDirectory(nullptr);
  graph_trend_ = std::make_unique<TGraph>();
  graph_trend_->SetTitle("tof trend;event_number;tof");

  if (!RegisterCanvas(canvas_board_.get(), error_text) ||
      !RegisterCanvas(canvas_channel_.get(), error_text) ||
      !RegisterCanvas(canvas_trend_.get(), error_text)) {
    return false;
  }

  if (!RegisterDrawable(canvas_board_.get(), hist_board_.get(), "", error_text) ||
      !RegisterDrawable(canvas_channel_.get(), hist_channel_.get(), "", error_text) ||
      !RegisterDrawable(canvas_trend_.get(), graph_trend_.get(), "AL", error_text)) {
    return false;
  }

  return true;
}

bool Kc705TofOverviewAnalysis::Event(const Kc705TofEvent& Event, std::string& error_text) {
  error_text.clear();
  hist_board_->Fill(static_cast<double>(Event.board_id));
  hist_channel_->Fill(static_cast<double>(Event.channel_id));

  graph_trend_->AddPoint(static_cast<double>(Event.event_number), static_cast<double>(Event.tof));
  while (static_cast<std::size_t>(graph_trend_->GetN()) > trend_points_) {
    graph_trend_->RemovePoint(0);
  }

  return true;
}

bool Kc705TofOverviewAnalysis::Finalise(std::string& error_text) {
  error_text.clear();
  return true;
}

const char* Kc705TofOverviewAnalysisFactory::Name() const { return "kc705_tof_overview"; }

const char* Kc705TofOverviewAnalysisFactory::ExpectedDecoder() const { return "kc705_tof"; }

bool Kc705TofOverviewAnalysisFactory::Create(const ParsedAnalysisSpec& spec,
                                             std::unique_ptr<IRealtimeAnalysis>& out_analysis,
                                             std::string& error_text) const {
  std::size_t trend_points = 1000;
  if (!parse_overview_spec(spec, trend_points, error_text)) {
    return false;
  }

  out_analysis = std::make_unique<Kc705TofOverviewAnalysis>(trend_points);
  error_text.clear();
  return true;
}

const IMonitorRealtimeAnalysisFactory& GetKc705TofOverviewAnalysisFactory() {
  static Kc705TofOverviewAnalysisFactory factory;
  return factory;
}

REGISTER_ANALYSIS(GetKc705TofOverviewAnalysisFactory);
