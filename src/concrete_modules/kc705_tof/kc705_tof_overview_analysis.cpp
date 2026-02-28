#include "concrete_modules/kc705_tof/kc705_tof_overview_analysis.hpp"

#include <algorithm>
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
  canvas_board_->SetLeftMargin(0.15);
  canvas_board_->SetBottomMargin(0.15);
  canvas_board_->SetRightMargin(0.05);
  canvas_channel_->SetLeftMargin(0.15);
  canvas_channel_->SetBottomMargin(0.15);
  canvas_channel_->SetRightMargin(0.05);
  canvas_trend_->SetLeftMargin(0.15);
  canvas_trend_->SetBottomMargin(0.15);
  canvas_trend_->SetRightMargin(0.05);
  canvas_board_->SetLogy();
  canvas_channel_->SetLogy();
  
  canvas_tofs_ = std::make_unique<TCanvas>("kc705_tofs_canvas", "KC705 TOF: TOFs", 1500, 1000);
  canvas_tofs_->Divide(4, 4);
  for (int i = 1; i <= 16; ++i) {
    auto* pad = canvas_tofs_->cd(i);
    if (pad != nullptr) {
      pad->SetLeftMargin(0.15);
      pad->SetBottomMargin(0.15);
      pad->SetRightMargin(0.05);
    }
  }

  hist_board_ = std::make_unique<TH1D>("kc705_board_hist", "Board ID;Board;counts", 2, 0, 2);
  hist_board_->GetXaxis()->SetBinLabel(1, "Board 0x000");
  hist_board_->GetXaxis()->SetBinLabel(2, "Board 0x111");
  hist_board_->SetFillColor(kBlue-7);
  hist_board_->SetStats(kFALSE);
  hist_board_->SetMarkerSize(3.0);

  hist_channel_ = std::make_unique<TH1D>("kc705_channel_hist", "Channel ID;Channel;counts", 13, 1, 14);
  hist_board_->SetDirectory(nullptr);
  hist_channel_->SetDirectory(nullptr);
  hist_channel_->SetFillColor(kBlue-7);
  hist_channel_->SetStats(kFALSE);
  hist_channel_->SetMarkerSize(2.4);
  for(int i = 1; i <= 13; ++i) {
    hist_channel_->GetXaxis()->SetBinLabel(i, Form("%d", i));
  }

  hist_tofs_.reserve(13);
  for (int i = 0; i < 13; ++i) {
    auto hist_tof = std::make_unique<TH1D>(Form("kc705_tof_hist_ch%02d", i+1), 
                                           Form("Time Channel %d;Time - DAQ Start (min);Counts", i+1), 
                                           1000, 0, 3600*5/60.0);
    hist_tof->SetDirectory(nullptr);
    hist_tof->SetFillColor(kBlue-7);
    hist_tof->SetStats(kFALSE);
    hist_tofs_.push_back(std::move(hist_tof));
  } 
  graph_trend_ = std::make_unique<TGraph>();
  graph_trend_->SetTitle("time trend;event_number;Time - DAQ Start (min)");

  auto apply_axis_text_style = [](TH1D* hist, Double_t label_size, Double_t title_size) {
    if (hist == nullptr) {
      return;
    }
    hist->GetXaxis()->SetLabelSize(label_size);
    hist->GetYaxis()->SetLabelSize(label_size);
    hist->GetXaxis()->SetTitleSize(title_size);
    hist->GetYaxis()->SetTitleSize(title_size);
    hist->GetXaxis()->SetTitleOffset(0.95);
    hist->GetYaxis()->SetTitleOffset(1.0);
  };
  apply_axis_text_style(hist_board_.get(), 0.06, 0.07);
  apply_axis_text_style(hist_channel_.get(), 0.055, 0.065);
  for (auto& hist_tof : hist_tofs_) {
    apply_axis_text_style(hist_tof.get(), 0.06, 0.07);
  }
  graph_trend_->GetXaxis()->SetLabelSize(0.05);
  graph_trend_->GetYaxis()->SetLabelSize(0.05);
  graph_trend_->GetXaxis()->SetTitleSize(0.06);
  graph_trend_->GetYaxis()->SetTitleSize(0.06);
  graph_trend_->GetXaxis()->SetTitleOffset(0.95);
  graph_trend_->GetYaxis()->SetTitleOffset(1.0);

  if (!RegisterCanvas(canvas_board_.get(), error_text) ||
      !RegisterCanvas(canvas_channel_.get(), error_text) ||
      !RegisterCanvas(canvas_trend_.get(), error_text) ||
      !RegisterCanvas(canvas_tofs_.get(), error_text)) {
    return false;
  }

  if (!RegisterDrawable(canvas_board_.get(), hist_board_.get(), "HIST TEXT0", error_text) ||
      !RegisterDrawable(canvas_channel_.get(), hist_channel_.get(), "HIST TEXT", error_text) ||
      !RegisterDrawable(canvas_trend_.get(), graph_trend_.get(), "AL", error_text)) {
    return false;
  }
  for (int i = 0; i < 13; ++i) {
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
  daq_start_time_[0] = daq_start_time_[1] = -1;
  
  return true;
}

bool Kc705TofOverviewAnalysis::EndOfRun(uint32_t run_number, std::string& error_text) {
  error_text.clear();
  return true;
}

bool Kc705TofOverviewAnalysis::Event(const Kc705TofEvent& Event, std::string& error_text) {
  error_text.clear();
  
  size_t board_index = (Event.board_id == 0x000) ? 0 : 1;
  if (daq_start_time_[board_index] < 0) {
    daq_start_time_[board_index] = Event.time;
  }
  
  // Skip the periodic channels
  if(Event.channel_id == 1  || Event.channel_id == 2 || Event.channel_id == 7 || Event.channel_id == 8) {
    return true;
  }

  hist_board_  ->Fill(0.5 + static_cast<double>(board_index));
  hist_channel_->Fill(0.5 + static_cast<double>(Event.channel_id));

  Double_t time_in_minutes = (Event.time - daq_start_time_[board_index]) / 60.0;
  min_time_ = std::min(min_time_, time_in_minutes);
  max_time_ = std::max(max_time_, time_in_minutes);

  graph_trend_->AddPoint(static_cast<double>(Event.event_number), time_in_minutes);
  while (static_cast<std::size_t>(graph_trend_->GetN()) > trend_points_) {
    graph_trend_->RemovePoint(0);
  }
  
  if (Event.channel_id-1 < hist_tofs_.size()) {
    hist_tofs_[Event.channel_id-1]->Fill(time_in_minutes);
  }

  return true;
}

bool Kc705TofOverviewAnalysis::UpdateDrawables(std::string& error_text) {
  error_text.clear();

  if (max_time_ < min_time_) {
    return true;
  }

  if (hist_tofs_.empty()) {
    return true;
  }

  const Double_t margin = 0.1 * (max_time_ - min_time_);
  auto* reference_axis = hist_tofs_.front()->GetXaxis();
  const Double_t axis_min = reference_axis->GetXmin();
  const Double_t axis_max = reference_axis->GetXmax();
  const Double_t range_min = std::clamp(min_time_ - margin, axis_min, axis_max);
  const Double_t range_max = std::clamp(max_time_ + margin, axis_min, axis_max);
  const Double_t apply_min = (range_min <= range_max) ? range_min : axis_min;
  const Double_t apply_max = (range_min <= range_max) ? range_max : axis_max;

  for (auto& hist_tof : hist_tofs_) {
    auto* axis = hist_tof->GetXaxis();
    axis->SetRangeUser(apply_min, apply_max);
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
