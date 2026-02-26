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
  canvas_board_ = std::make_unique<TCanvas>("kc705_board_canvas", "KC705 TOF: Board ID");
  canvas_channel_ = std::make_unique<TCanvas>("kc705_channel_canvas", "KC705 TOF: Channel ID");
  canvas_trend_ = std::make_unique<TCanvas>("kc705_value_trend_canvas", "KC705 TOF: Value Trend");
  
  canvas_tofs_ = std::make_unique<TCanvas>("kc705_tofs_canvas", "KC705 TOF: TOFs", 1200, 900);
  canvas_tofs_->Divide(4, 4);

  hist_board_ = std::make_unique<TH1D>("kc705_board_hist", "Board ID;board_id;counts", 2, 0, 2);
  hist_board_->GetXaxis()->SetBinLabel(1, "Board 0x000");
  hist_board_->GetXaxis()->SetBinLabel(2, "Board 0x111");
  hist_board_->SetFillColor(kBlue-7);
  hist_board_->SetStats(kFALSE);

  hist_channel_ = std::make_unique<TH1D>("kc705_channel_hist", "Channel ID;channel_id;counts", 16, 0, 16);
  hist_board_->SetDirectory(nullptr);
  hist_channel_->SetDirectory(nullptr);
  hist_channel_->SetFillColor(kBlue-7);
  hist_channel_->SetStats(kFALSE);

  hist_tofs_.reserve(16);
  for (int i = 0; i < 16; ++i) {
    auto hist_tof = std::make_unique<TH1D>(Form("kc705_tof_hist_ch%02d", i), 
                                           Form("Time Channel %d;time;counts", i), 
                                           3600, 0, 3600);
    hist_tof->SetDirectory(nullptr);
    hist_tof->SetFillColor(kBlue-7);
    hist_tof->SetStats(kFALSE);
    hist_tofs_.push_back(std::move(hist_tof));
  } 
  graph_trend_ = std::make_unique<TGraph>();
  graph_trend_->SetTitle("time trend;event_number;time");

  if (!RegisterCanvas(canvas_board_.get(), error_text) ||
      !RegisterCanvas(canvas_channel_.get(), error_text) ||
      !RegisterCanvas(canvas_trend_.get(), error_text) ||
      !RegisterCanvas(canvas_tofs_.get(), error_text)) {
    return false;
  }

  if (!RegisterDrawable(canvas_board_.get(), hist_board_.get(), "", error_text) ||
      !RegisterDrawable(canvas_channel_.get(), hist_channel_.get(), "", error_text) ||
      !RegisterDrawable(canvas_trend_.get(), graph_trend_.get(), "AL", error_text)) {
    return false;
  }
  for (int i = 0; i < 16; ++i) {
    auto pad = canvas_tofs_->cd(i+1);
    if (!RegisterDrawable(pad, hist_tofs_[i].get(), "", error_text)) {
      return false;
    }
  }

  return true;
}

bool Kc705TofOverviewAnalysis::BeginOfRun(uint32_t run_number, std::string& error_text) {
  error_text.clear();
  
  hist_board_->Reset();
  hist_channel_->Reset();
  for (auto& hist_tof : hist_tofs_) {
    hist_tof->Reset();
  }
  graph_trend_->Set(0);
  min_time_ = 1e100;
  max_time_ = -1e100;
  
  return true;
}

bool Kc705TofOverviewAnalysis::EndOfRun(uint32_t run_number, std::string& error_text) {
  error_text.clear();
  return true;
}

bool Kc705TofOverviewAnalysis::Event(const Kc705TofEvent& Event, std::string& error_text) {
  error_text.clear();
  hist_board_  ->Fill( (0==static_cast<double>(Event.board_id)) ? 0.5 : 1.5 );
  hist_channel_->Fill(static_cast<double>(Event.channel_id)+0.5);

  graph_trend_->AddPoint(static_cast<double>(Event.event_number), static_cast<double>(Event.time));
  while (static_cast<std::size_t>(graph_trend_->GetN()) > trend_points_) {
    graph_trend_->RemovePoint(0);
  }
  
  if (Event.channel_id < hist_tofs_.size()) {
    hist_tofs_[Event.channel_id]->Fill(Event.time);

    min_time_ = std::min(min_time_, Event.time);
    max_time_ = std::max(max_time_, Event.time);
  }

  return true;
}

bool Kc705TofOverviewAnalysis::UpdateDrawables(std::string& error_text) {
  error_text.clear();

  if (max_time_ < min_time_) {
    return true;
  }

  Double_t margin = 0.1 * (max_time_ - min_time_);
  for(auto& hist_tof : hist_tofs_) {
    hist_tof->GetXaxis()->SetRangeUser(min_time_ - margin, max_time_ + margin);
  }

  return true;
}

bool Kc705TofOverviewAnalysis::Finalise(std::string& error_text) {
  error_text.clear();
  return true;
}

const char* Kc705TofOverviewAnalysisFactory::Name() const { return "kc705_tof_overview"; }

const char* Kc705TofOverviewAnalysisFactory::Title() const { return "KC705 TOF Overview"; }

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
