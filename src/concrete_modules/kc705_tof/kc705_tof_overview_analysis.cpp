#include "concrete_modules/kc705_tof/kc705_tof_overview_analysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <TCanvas.h>
#include <TGraph.h>
#include <TH1D.h>
#include <TLatex.h>

#include "concrete_modules/module_registry.hpp"

namespace {

constexpr Double_t kPeriodicRateWindowSec = 5.0;
constexpr Double_t kPeriodicTrendRetentionMin = 10.0;
constexpr Double_t kPeriodicPairGapMs = 20.0;
constexpr Double_t kReconfigExclusionMarginSec = 3.0;
constexpr Double_t kTofMinMs = -2.0;
constexpr Double_t kTofMaxMs = 40.0;
constexpr Double_t kTofMinUs = 1.0;
constexpr Double_t kTofMaxUs = 4.0;
constexpr std::size_t kTofGroupSize = 3;
constexpr std::size_t kPeriodicTrendPoints = 1000;

std::optional<std::filesystem::path> find_exclusion_summary_dir() {
  const char* env_value = std::getenv("DCSLOGDIR");
  if (env_value == nullptr || env_value[0] == '\0') {
    return std::nullopt;
  }

  const std::filesystem::path candidate(env_value);
  std::error_code ec;
  if (std::filesystem::is_directory(candidate, ec)) {
    return candidate;
  }
  return std::nullopt;
}

std::vector<std::string> split_tsv_line(const std::string& line) {
  std::vector<std::string> columns;
  std::stringstream ss(line);
  std::string field;
  while (std::getline(ss, field, '\t')) {
    columns.push_back(field);
  }
  return columns;
}

struct TofGroupDef {
  const char* name;
  std::size_t first_index;
  std::size_t count;
};

inline constexpr std::array<TofGroupDef, 4> kTofGroupDefs = {{
    {"RECBE", 0, 3},
    {"MKii", 3, 3},
    {"ROESTI", 6, 3},
    {"All", 0, Kc705TofNamedChannelCount()},
}};

void update_graph_axis_style(TGraph* graph, Double_t label_size, Double_t title_size) {
  if (graph == nullptr) {
    return;
  }
  graph->GetXaxis()->SetLabelSize(label_size);
  graph->GetYaxis()->SetLabelSize(label_size);
  graph->GetXaxis()->SetTitleSize(title_size);
  graph->GetYaxis()->SetTitleSize(title_size);
  graph->GetXaxis()->SetTitleOffset(0.95);
  graph->GetYaxis()->SetTitleOffset(1.0);
}

bool graph_x_range(const TGraph* graph, Double_t& out_min, Double_t& out_max) {
  if (graph == nullptr || graph->GetN() <= 0) {
    return false;
  }

  out_min = graph->GetPointX(0);
  out_max = out_min;
  for (int i = 1; i < graph->GetN(); ++i) {
    const Double_t x = graph->GetPointX(i);
    out_min = std::min(out_min, x);
    out_max = std::max(out_max, x);
  }
  return true;
}

void prune_graph_before_time(TGraph* graph, Double_t min_time_in_minutes) {
  if (graph == nullptr) {
    return;
  }
  while (graph->GetN() > 0 && graph->GetPointX(0) < min_time_in_minutes) {
    graph->RemovePoint(0);
  }
}

int to_canvas_grid(int n_pads) {
  if (n_pads <= 0) {
    return 1;
  }
  return static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n_pads))));
}

bool parse_overview_spec(const ParsedAnalysisSpec& spec, std::string& error_text) {
  if (!spec.RejectUnknown({"exclude_reconfig"}, error_text)) {
    return false;
  }

  return true;
}

bool value_is_in_histogram_range(const TH1D* hist, Double_t value) {
  if (hist == nullptr) {
    return false;
  }
  const auto* axis = hist->GetXaxis();
  return axis != nullptr && value >= axis->GetXmin() && value < axis->GetXmax();
}

void fill_histogram_if_in_range(TH1D* hist, Double_t value) {
  if (value_is_in_histogram_range(hist, value)) {
    hist->Fill(value);
  }
}

std::unique_ptr<TLatex> make_entry_label(const char* name) {
  auto label = std::make_unique<TLatex>();
  label->SetName(name);
  label->SetNDC(kTRUE);
  label->SetTextAlign(13);
  label->SetTextSize(0.055);
  return label;
}

void update_entry_label(TLatex* label, const TH1D* hist) {
  if (label == nullptr || hist == nullptr) {
    return;
  }
  label->SetText(0.18, 0.86, Form("Entries: %.0f", hist->GetEntries()));
}

}  // namespace

bool Kc705TofOverviewAnalysis::Initialise(std::string& error_text) {
  error_text.clear();
  if (enable_exclusion_filter_ && !LoadExcludedTimeRanges(error_text)) {
    return false;
  }
  // Keep the summary plots separate from the per-channel timing plots.
  canvas_board_ = std::make_unique<TCanvas>("kc705_board_canvas", "KC705 TOF: Board ID");
  canvas_channel_ = std::make_unique<TCanvas>("kc705_channel_canvas", "KC705 TOF: Channel ID");
  canvas_channel_named_ = std::make_unique<TCanvas>("kc705_channel_named_canvas", "KC705 TOF: Channel Name");
  canvas_periodic_rate_trend_ =
      std::make_unique<TCanvas>("kc705_periodic_rate_trend_canvas", "KC705 TOF: Periodic Signal Frequency");
  canvas_tdcs_ = std::make_unique<TCanvas>("kc705_tdcs_canvas", "KC705 TOF: TDCs", 1500, 1000);
  canvas_board_->SetLeftMargin(0.15);
  canvas_board_->SetBottomMargin(0.15);
  canvas_board_->SetRightMargin(0.05);
  canvas_channel_->SetLeftMargin(0.15);
  canvas_channel_->SetBottomMargin(0.15);
  canvas_channel_->SetRightMargin(0.05);
  canvas_channel_named_->SetLeftMargin(0.15);
  canvas_channel_named_->SetBottomMargin(0.15);
  canvas_channel_named_->SetRightMargin(0.05);
  canvas_periodic_rate_trend_->SetLeftMargin(0.15);
  canvas_periodic_rate_trend_->SetBottomMargin(0.15);
  canvas_periodic_rate_trend_->SetRightMargin(0.05);
  canvas_periodic_rate_trend_->Divide(1, 2);
  canvas_board_->SetLogy();
  canvas_channel_->SetLogy();
  canvas_channel_named_->SetLogy();
  for (int i = 1; i <= 2; ++i) {
    auto* pad = canvas_periodic_rate_trend_->cd(i);
    if (pad != nullptr) {
      pad->SetLeftMargin(0.15);
      pad->SetBottomMargin(0.15);
      pad->SetRightMargin(0.05);
    }
  }
  canvas_tofs_ = std::make_unique<TCanvas>("kc705_tofs_canvas", "KC705 TOF: TOFs", 1500, 1000);
  canvas_tofs_us_ = std::make_unique<TCanvas>("kc705_tofs_us_canvas", "KC705 TOF: TOFs (0-2 us)", 1500, 1000);
  canvas_tof_groups_ = std::make_unique<TCanvas>("kc705_tof_groups_canvas", "KC705 TOF: Grouped TOFs", 1200, 900);
  canvas_tof_groups_us_ =
      std::make_unique<TCanvas>("kc705_tof_groups_us_canvas", "KC705 TOF: Grouped TOFs (0-2 us)", 1200, 900);
  const int num_named_channels = static_cast<int>(Kc705TofNamedChannelCount());
  const int num_tof_columns = to_canvas_grid(num_named_channels);
  const int num_tof_rows = static_cast<int>(std::ceil(static_cast<double>(num_named_channels) / num_tof_columns));
  canvas_tdcs_->Divide(num_tof_columns, num_tof_rows);
  canvas_tofs_->Divide(num_tof_columns, num_tof_rows);
  canvas_tofs_us_->Divide(num_tof_columns, num_tof_rows);
  canvas_tof_groups_->Divide(2, 2);
  canvas_tof_groups_us_->Divide(2, 2);
  for (int i = 1; i <= num_named_channels; ++i) {
    auto* tdc_pad = canvas_tdcs_->cd(i);
    if (tdc_pad != nullptr) {
      tdc_pad->SetLeftMargin(0.15);
      tdc_pad->SetBottomMargin(0.15);
      tdc_pad->SetRightMargin(0.05);
      tdc_pad->SetLogy();
    }
    auto* tof_pad = canvas_tofs_->cd(i);
    if (tof_pad != nullptr) {
      tof_pad->SetLeftMargin(0.15);
      tof_pad->SetBottomMargin(0.15);
      tof_pad->SetRightMargin(0.05);
      tof_pad->SetLogy();
    }
    auto* tof_us_pad = canvas_tofs_us_->cd(i);
    if (tof_us_pad != nullptr) {
      tof_us_pad->SetLeftMargin(0.15);
      tof_us_pad->SetBottomMargin(0.15);
      tof_us_pad->SetRightMargin(0.05);
    }
  }
  for (int i = 1; i <= 4; ++i) {
    auto* tof_group_pad = canvas_tof_groups_->cd(i);
    if (tof_group_pad != nullptr) {
      tof_group_pad->SetLeftMargin(0.15);
      tof_group_pad->SetBottomMargin(0.15);
      tof_group_pad->SetRightMargin(0.05);
      tof_group_pad->SetLogy();
    }
    auto* tof_group_us_pad = canvas_tof_groups_us_->cd(i);
    if (tof_group_us_pad != nullptr) {
      tof_group_us_pad->SetLeftMargin(0.15);
      tof_group_us_pad->SetBottomMargin(0.15);
      tof_group_us_pad->SetRightMargin(0.05);
    }
  }
  for (int i = num_named_channels + 1; i <= num_tof_columns * num_tof_rows; ++i) {
    auto* tdc_pad = canvas_tdcs_->cd(i);
    if (tdc_pad != nullptr) {
      tdc_pad->SetLeftMargin(0.15);
      tdc_pad->SetBottomMargin(0.15);
      tdc_pad->SetRightMargin(0.05);
    }
    auto* tof_pad = canvas_tofs_->cd(i);
    if (tof_pad != nullptr) {
      tof_pad->SetLeftMargin(0.15);
      tof_pad->SetBottomMargin(0.15);
      tof_pad->SetRightMargin(0.05);
    }
    auto* tof_us_pad = canvas_tofs_us_->cd(i);
    if (tof_us_pad != nullptr) {
      tof_us_pad->SetLeftMargin(0.15);
      tof_us_pad->SetBottomMargin(0.15);
      tof_us_pad->SetRightMargin(0.05);
    }
  }

  hist_board_ = std::make_unique<TH1D>("kc705_board_hist", "Board ID;Board;counts", 2, 0, 2);
  hist_board_->GetXaxis()->SetBinLabel(1, "Board 0x000");
  hist_board_->GetXaxis()->SetBinLabel(2, "Board 0x111");
  hist_board_->SetFillColor(kBlue-7);
  hist_board_->SetStats(kFALSE);
  hist_board_->SetMarkerSize(3.0);

  hist_channel_ = std::make_unique<TH1D>(
      "kc705_channel_hist",
      "Channel ID;Channel;counts",
      static_cast<int>(Kc705TofNamedChannelCount()),
      0,
      static_cast<double>(Kc705TofNamedChannelCount()));
  hist_channel_named_ = std::make_unique<TH1D>(
      "kc705_channel_named_hist",
      "Channel Name;;counts",
      static_cast<int>(Kc705TofNamedChannelCount()),
      0,
      static_cast<double>(Kc705TofNamedChannelCount()));
  hist_board_->SetDirectory(nullptr);
  hist_channel_->SetDirectory(nullptr);
  hist_channel_named_->SetDirectory(nullptr);
  hist_channel_->SetFillColor(kBlue-7);
  hist_channel_named_->SetFillColor(kBlue-7);
  hist_channel_->SetStats(kFALSE);
  hist_channel_named_->SetStats(kFALSE);
  hist_channel_->SetMarkerSize(2.4);
  hist_channel_named_->SetMarkerSize(1.8);
  for (std::size_t i = 0; i < Kc705TofNamedChannelCount(); ++i) {
    hist_channel_->GetXaxis()->SetBinLabel(
        static_cast<int>(i + 1),
        Form("%u", static_cast<unsigned>(kKc705TofChannelDefs[i].channel_id)));
  }
  for (std::size_t i = 0; i < Kc705TofNamedChannelCount(); ++i) {
    hist_channel_named_->GetXaxis()->SetBinLabel(static_cast<int>(i + 1), kKc705TofChannelDefs[i].name);
  }

  hist_tdcs_.reserve(Kc705TofNamedChannelCount());
  hist_tofs_.reserve(Kc705TofNamedChannelCount());
  hist_tofs_us_.reserve(Kc705TofNamedChannelCount());
  tof_entry_labels_.reserve(Kc705TofNamedChannelCount());
  tof_us_entry_labels_.reserve(Kc705TofNamedChannelCount());
  for (std::size_t i = 0; i < Kc705TofNamedChannelCount(); ++i) {
    const auto& channel_def = kKc705TofChannelDefs[i];
    // "TDC" keeps the original event time relative to the DAQ start.
    auto hist_tdc = std::make_unique<TH1D>(
        Form("kc705_tdc_hist_ch%02u", static_cast<unsigned>(channel_def.channel_id)),
        Form("TDC Channel %u (%s);Time - DAQ Start (min);Counts",
             static_cast<unsigned>(channel_def.channel_id),
             channel_def.name),
        1000,
        0,
        3600 * 5 / 60.0);
    hist_tdc->SetDirectory(nullptr);
    hist_tdc->SetFillColor(kBlue-7);
    hist_tdc->SetStats(kFALSE);
    hist_tdcs_.push_back(std::move(hist_tdc));

    // "TOF" is derived from the first pulse in each periodic doublet on the same board.
    auto hist_tof = std::make_unique<TH1D>(
        Form("kc705_tof_hist_ch%02u", static_cast<unsigned>(channel_def.channel_id)),
        Form("TOF Channel %u (%s);TOF (ms);Counts",
             static_cast<unsigned>(channel_def.channel_id),
             channel_def.name),
        200,
        kTofMinMs,
        kTofMaxMs);
    hist_tof->SetDirectory(nullptr);
    hist_tof->SetFillColor(kBlue - 7);
    hist_tof->SetStats(kFALSE);
    hist_tofs_.push_back(std::move(hist_tof));

    auto hist_tof_us = std::make_unique<TH1D>(
        Form("kc705_tof_us_hist_ch%02u", static_cast<unsigned>(channel_def.channel_id)),
        Form("TOF Channel %u (%s);TOF (us);Counts",
             static_cast<unsigned>(channel_def.channel_id),
             channel_def.name),
        200,
        kTofMinUs,
        kTofMaxUs);
    hist_tof_us->SetDirectory(nullptr);
    hist_tof_us->SetFillColor(kBlue - 7);
    hist_tof_us->SetStats(kFALSE);
    hist_tofs_us_.push_back(std::move(hist_tof_us));

    tof_entry_labels_.push_back(make_entry_label(Form("kc705_tof_entries_ch%02u", static_cast<unsigned>(channel_def.channel_id))));
    tof_us_entry_labels_.push_back(
        make_entry_label(Form("kc705_tof_us_entries_ch%02u", static_cast<unsigned>(channel_def.channel_id))));
  }
  hist_tof_groups_.reserve(kTofGroupDefs.size());
  hist_tof_groups_us_.reserve(kTofGroupDefs.size());
  tof_group_entry_labels_.reserve(kTofGroupDefs.size());
  tof_group_us_entry_labels_.reserve(kTofGroupDefs.size());
  for (const auto& group_def : kTofGroupDefs) {
    auto hist_tof_group = std::make_unique<TH1D>(
        Form("kc705_tof_group_hist_%s", group_def.name),
        Form("TOF Group %s;TOF (ms);Counts", group_def.name),
        200,
        kTofMinMs,
        kTofMaxMs);
    hist_tof_group->SetDirectory(nullptr);
    hist_tof_group->SetFillColor(kBlue - 7);
    hist_tof_group->SetStats(kFALSE);
    hist_tof_groups_.push_back(std::move(hist_tof_group));

    auto hist_tof_group_us = std::make_unique<TH1D>(
        Form("kc705_tof_group_us_hist_%s", group_def.name),
        Form("TOF Group %s;TOF (us);Counts", group_def.name),
        200,
        kTofMinUs,
        kTofMaxUs);
    hist_tof_group_us->SetDirectory(nullptr);
    hist_tof_group_us->SetFillColor(kBlue - 7);
    hist_tof_group_us->SetStats(kFALSE);
    hist_tof_groups_us_.push_back(std::move(hist_tof_group_us));

    tof_group_entry_labels_.push_back(make_entry_label(Form("kc705_tof_group_entries_%s", group_def.name)));
    tof_group_us_entry_labels_.push_back(make_entry_label(Form("kc705_tof_group_us_entries_%s", group_def.name)));
  }
  constexpr std::array<int, Kc705TofPeriodicChannelCount()> kPeriodicGraphColors = {kRed + 1, kBlue + 1};
  for (std::size_t i = 0; i < Kc705TofPeriodicChannelCount(); ++i) {
    periodic_rate_graphs_[i] = std::make_unique<TGraph>();
    periodic_rate_graphs_[i]->SetName(Form("kc705_periodic_rate_graph_ch%02u", kKc705TofPeriodicChannels[i]));
    periodic_rate_graphs_[i]->SetTitle(
        Form("Periodic signal frequency Ch %u;Time stamp - DAQ Start (min);Frequency (Hz)",
             static_cast<unsigned>(kKc705TofPeriodicChannels[i])));
    periodic_rate_graphs_[i]->SetLineColor(kPeriodicGraphColors[i]);
    periodic_rate_graphs_[i]->SetMarkerColor(kPeriodicGraphColors[i]);
    periodic_rate_graphs_[i]->SetMarkerStyle(20 + static_cast<int>(i));
    periodic_rate_graphs_[i]->SetMarkerSize(0.9);
    periodic_rate_graphs_[i]->SetLineWidth(2);
  }

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
  apply_axis_text_style(hist_channel_named_.get(), 0.05, 0.06);
  for (auto& hist_tdc : hist_tdcs_) {
    apply_axis_text_style(hist_tdc.get(), 0.06, 0.07);
  }
  for (auto& hist_tof : hist_tofs_) {
    apply_axis_text_style(hist_tof.get(), 0.06, 0.07);
  }
  for (auto& hist_tof_us : hist_tofs_us_) {
    apply_axis_text_style(hist_tof_us.get(), 0.06, 0.07);
  }
  for (auto& hist_tof_group : hist_tof_groups_) {
    apply_axis_text_style(hist_tof_group.get(), 0.06, 0.07);
  }
  for (auto& hist_tof_group_us : hist_tof_groups_us_) {
    apply_axis_text_style(hist_tof_group_us.get(), 0.06, 0.07);
  }
  for (auto& graph : periodic_rate_graphs_) {
    update_graph_axis_style(graph.get(), 0.05, 0.06);
  }

  if (!RegisterCanvas(canvas_board_.get(), error_text) ||
      !RegisterCanvas(canvas_channel_.get(), error_text) ||
      !RegisterCanvas(canvas_channel_named_.get(), error_text) ||
      !RegisterCanvas(canvas_periodic_rate_trend_.get(), error_text) ||
      !RegisterCanvas(canvas_tdcs_.get(), error_text) ||
      !RegisterCanvas(canvas_tofs_.get(), error_text) ||
      !RegisterCanvas(canvas_tofs_us_.get(), error_text) ||
      !RegisterCanvas(canvas_tof_groups_.get(), error_text) ||
      !RegisterCanvas(canvas_tof_groups_us_.get(), error_text)) {
    return false;
  }

  if (!RegisterDrawable(canvas_board_.get(), hist_board_.get(), "HIST TEXT0", error_text) ||
      !RegisterDrawable(canvas_channel_.get(), hist_channel_.get(), "HIST TEXT", error_text) ||
      !RegisterDrawable(canvas_channel_named_.get(), hist_channel_named_.get(), "HIST TEXT", error_text)) {
    return false;
  }
  for (std::size_t i = 0; i < Kc705TofPeriodicChannelCount(); ++i) {
    auto* pad = canvas_periodic_rate_trend_->cd(static_cast<int>(i + 1));
    if (!RegisterDrawable(pad, periodic_rate_graphs_[i].get(), "ALP", error_text)) {
      return false;
    }
  }
  for (std::size_t i = 0; i < hist_tdcs_.size(); ++i) {
    auto* tdc_pad = canvas_tdcs_->cd(static_cast<int>(i + 1));
    if (!RegisterDrawable(tdc_pad, hist_tdcs_[i].get(), "", error_text)) {
      return false;
    }
    auto* tof_pad = canvas_tofs_->cd(static_cast<int>(i + 1));
    if (!RegisterDrawable(tof_pad, hist_tofs_[i].get(), "", error_text)) {
      return false;
    }
    if (!RegisterDrawable(tof_pad, tof_entry_labels_[i].get(), "SAME", error_text)) {
      return false;
    }
    auto* tof_us_pad = canvas_tofs_us_->cd(static_cast<int>(i + 1));
    if (!RegisterDrawable(tof_us_pad, hist_tofs_us_[i].get(), "", error_text)) {
      return false;
    }
    if (!RegisterDrawable(tof_us_pad, tof_us_entry_labels_[i].get(), "SAME", error_text)) {
      return false;
    }
  }
  for (std::size_t i = 0; i < hist_tof_groups_.size(); ++i) {
    auto* tof_group_pad = canvas_tof_groups_->cd(static_cast<int>(i + 1));
    if (!RegisterDrawable(tof_group_pad, hist_tof_groups_[i].get(), "", error_text)) {
      return false;
    }
    if (!RegisterDrawable(tof_group_pad, tof_group_entry_labels_[i].get(), "SAME", error_text)) {
      return false;
    }
    auto* tof_group_us_pad = canvas_tof_groups_us_->cd(static_cast<int>(i + 1));
    if (!RegisterDrawable(tof_group_us_pad, hist_tof_groups_us_[i].get(), "", error_text)) {
      return false;
    }
    if (!RegisterDrawable(tof_group_us_pad, tof_group_us_entry_labels_[i].get(), "SAME", error_text)) {
      return false;
    }
  }

  return true;
}

bool Kc705TofOverviewAnalysis::BeginOfRun(uint32_t run_number, std::string& error_text) {
  error_text.clear();
  static_cast<void>(run_number);
  
  hist_board_->Reset();
  hist_channel_->Reset();
  hist_channel_named_->Reset();
  for (auto& hist_tdc : hist_tdcs_) {
    hist_tdc->Reset();
  }
  for (auto& hist_tof : hist_tofs_) {
    hist_tof->Reset();
  }
  for (auto& hist_tof_us : hist_tofs_us_) {
    hist_tof_us->Reset();
  }
  for (auto& hist_tof_group : hist_tof_groups_) {
    hist_tof_group->Reset();
  }
  for (auto& hist_tof_group_us : hist_tof_groups_us_) {
    hist_tof_group_us->Reset();
  }
  min_time_ = 1e100;
  max_time_ = -1e100;
  excluded_event_count_ = 0;
  excluded_event_count_by_channel_.fill(0);
  daq_start_time_[0] = daq_start_time_[1] = -1;
  last_periodic_time_ms_ = {0.0, 0.0};
  first_periodic_time_ms_ = {0.0, 0.0};
  has_last_periodic_time_ms_ = {false, false};
  has_first_periodic_time_ms_ = {false, false};
  for (auto& graph : periodic_rate_graphs_) {
    graph->Set(0);
  }
  for (auto& event_times : periodic_event_times_) {
    event_times.clear();
  }
  
  return true;
}

bool Kc705TofOverviewAnalysis::EndOfRun(uint32_t run_number, std::string& error_text) {
  error_text.clear();
  if (enable_exclusion_filter_) {
    std::cout << "[kc705_tof_overview] run " << run_number
              << " excluded " << excluded_event_count_ << " events";
    bool printed_channel_detail = false;
    for (std::size_t i = 0; i < excluded_event_count_by_channel_.size(); ++i) {
      if (excluded_event_count_by_channel_[i] == 0) {
        continue;
      }
      std::cout << (printed_channel_detail ? "," : " (");
      std::cout << kKc705TofChannelDefs[i].summary_name << "=" << excluded_event_count_by_channel_[i];
      printed_channel_detail = true;
    }
    if (printed_channel_detail) {
      std::cout << ")";
    }
    std::cout << '\n';
  }
  return true;
}

bool Kc705TofOverviewAnalysis::LoadExcludedTimeRanges(std::string& error_text) {
  error_text.clear();
  for (auto& ranges : excluded_time_ranges_) {
    ranges.clear();
  }

  const auto summary_dir = find_exclusion_summary_dir();
  if (!summary_dir.has_value()) {
    error_text = "reconfiguration exclusion is enabled but DCSLOGDIR is unset or not a directory";
    return false;
  }

  for (std::size_t i = 0; i < kKc705TofChannelDefs.size(); ++i) {
    const auto& channel_def = kKc705TofChannelDefs[i];
    if (channel_def.summary_name == nullptr || channel_def.summary_name[0] == '\0') {
      continue;
    }

    const auto summary_path = *summary_dir / (std::string(channel_def.summary_name) + ".tsv");
    std::ifstream ifs(summary_path);
    if (!ifs.is_open()) {
      error_text = "required exclusion summary file not found: " + summary_path.string();
      return false;
    }

    std::string line;
    bool is_first_line = true;
    while (std::getline(ifs, line)) {
      if (line.empty()) {
        continue;
      }
      if (is_first_line) {
        is_first_line = false;
        continue;
      }

      const auto columns = split_tsv_line(line);
      if (columns.size() < 4) {
        error_text = "invalid exclusion summary row in " + summary_path.string();
        return false;
      }

      try {
        const Double_t start_unix = std::stod(columns[0]);
        const Double_t end_unix = std::stod(columns[2]);
        excluded_time_ranges_[i].push_back({start_unix, end_unix});
      } catch (const std::exception&) {
        error_text = "invalid exclusion summary timestamp in " + summary_path.string();
        return false;
      }
    }

    std::sort(
        excluded_time_ranges_[i].begin(),
        excluded_time_ranges_[i].end(),
        [](const ExclusionRange& lhs, const ExclusionRange& rhs) { return lhs.start_unix < rhs.start_unix; });
  }

  return true;
}

bool Kc705TofOverviewAnalysis::IsExcludedByTimestamp(const Kc705TofEvent& event) const {
  if (!std::isfinite(event.timestamp)) {
    return false;
  }

  const auto sort_index = Kc705TofChannelSortIndex(event.channel_id);
  if (!sort_index.has_value() || *sort_index >= excluded_time_ranges_.size()) {
    return false;
  }

  for (const auto& range : excluded_time_ranges_[*sort_index]) {
    const Double_t window_start = range.start_unix - kReconfigExclusionMarginSec;
    const Double_t window_end = range.end_unix + kReconfigExclusionMarginSec;
    if (event.timestamp >= window_start && event.timestamp <= window_end) {
      return true;
    }
    if (event.timestamp < window_start) {
      return false;
    }
  }
  return false;
}

bool Kc705TofOverviewAnalysis::Event(const Kc705TofEvent& Event, std::string& error_text) {
  error_text.clear();
  if (IsExcludedByTimestamp(Event)) {
    ++excluded_event_count_;
    if (const auto sort_index = Kc705TofChannelSortIndex(Event.channel_id); sort_index.has_value()) {
      ++excluded_event_count_by_channel_[*sort_index];
    }
    return true;
  }
  
  size_t board_index = (Event.board_id == 0x000) ? 0 : 1;
  if (daq_start_time_[board_index] < 0) {
    daq_start_time_[board_index] = Event.time;
  }
  const Double_t time_in_minutes = (Event.time - daq_start_time_[board_index]) / 60.0;
  const Double_t event_time_ms = Event.time * 1.0e3;
  
  if (const auto periodic_index = PeriodicChannelIndex(Event.channel_id); periodic_index.has_value()) {
    auto& event_times = periodic_event_times_[*periodic_index];
    event_times.push_back(Event.time);
    const Double_t cutoff_time = Event.time - kPeriodicRateWindowSec;
    while (!event_times.empty() && event_times.front() < cutoff_time) {
      event_times.pop_front();
    }
    const Double_t count_rate_hz = static_cast<Double_t>(event_times.size()) / kPeriodicRateWindowSec;
    auto* periodic_graph = periodic_rate_graphs_[*periodic_index].get();
    periodic_graph->AddPoint(time_in_minutes, count_rate_hz);
    prune_graph_before_time(periodic_graph, time_in_minutes - kPeriodicTrendRetentionMin);
    while (static_cast<std::size_t>(periodic_graph->GetN()) > kPeriodicTrendPoints) {
      periodic_graph->RemovePoint(0);
    }

    // The periodic channel emits a double pulse; only the first pulse is used as the TOF reference.
    const bool is_first_pulse =
        !has_last_periodic_time_ms_[board_index] ||
        (event_time_ms - last_periodic_time_ms_[board_index]) > kPeriodicPairGapMs;
    last_periodic_time_ms_[board_index] = event_time_ms;
    has_last_periodic_time_ms_[board_index] = true;
    if (is_first_pulse) {
      first_periodic_time_ms_[board_index] = event_time_ms;
      has_first_periodic_time_ms_[board_index] = true;
    }
    return true;
  }

  hist_board_  ->Fill(0.5 + static_cast<double>(board_index));
  if (const auto sort_index = Kc705TofChannelSortIndex(Event.channel_id); sort_index.has_value()) {
    hist_channel_->Fill(0.5 + static_cast<double>(*sort_index));
    hist_channel_named_->Fill(0.5 + static_cast<double>(*sort_index));
    if (*sort_index < hist_tdcs_.size()) {
      hist_tdcs_[*sort_index]->Fill(time_in_minutes);
    }
    // Fill the physical TOF only after a first periodic pulse has been observed on this board.
    if (*sort_index < hist_tofs_.size() && has_first_periodic_time_ms_[board_index]) {
      const Double_t tof_ms = event_time_ms - first_periodic_time_ms_[board_index];
      fill_histogram_if_in_range(hist_tofs_[*sort_index].get(), tof_ms);
      if (*sort_index < hist_tofs_us_.size()) {
        fill_histogram_if_in_range(hist_tofs_us_[*sort_index].get(), tof_ms * 1.0e3);
      }
      const std::size_t group_index = *sort_index / kTofGroupSize;
      if (group_index < hist_tof_groups_.size()) {
        fill_histogram_if_in_range(hist_tof_groups_[group_index].get(), tof_ms);
      }
      if (group_index < hist_tof_groups_us_.size()) {
        fill_histogram_if_in_range(hist_tof_groups_us_[group_index].get(), tof_ms * 1.0e3);
      }
      if (hist_tof_groups_.size() > 3) {
        fill_histogram_if_in_range(hist_tof_groups_[3].get(), tof_ms);
      }
      if (hist_tof_groups_us_.size() > 3) {
        fill_histogram_if_in_range(hist_tof_groups_us_[3].get(), tof_ms * 1.0e3);
      }
    }
  } else {
    return true;
  }

  min_time_ = std::min(min_time_, time_in_minutes);
  max_time_ = std::max(max_time_, time_in_minutes);
  
  return true;
}

bool Kc705TofOverviewAnalysis::UpdateDrawables(std::string& error_text) {
  error_text.clear();

  if (!hist_tdcs_.empty() && max_time_ >= min_time_) {
    // Keep all TDC panels on the same visible time window.
    const Double_t margin = 0.1 * (max_time_ - min_time_);
    auto* reference_axis = hist_tdcs_.front()->GetXaxis();
    const Double_t axis_min = reference_axis->GetXmin();
    const Double_t axis_max = reference_axis->GetXmax();
    const Double_t range_min = std::clamp(min_time_ - margin, axis_min, axis_max);
    const Double_t range_max = std::clamp(max_time_ + margin, axis_min, axis_max);
    const Double_t apply_min = (range_min <= range_max) ? range_min : axis_min;
    const Double_t apply_max = (range_min <= range_max) ? range_max : axis_max;

    for (auto& hist_tdc : hist_tdcs_) {
      auto* axis = hist_tdc->GetXaxis();
      axis->SetRangeUser(apply_min, apply_max);
    }
  }

  for (auto& graph : periodic_rate_graphs_) {
    Double_t graph_min = 0.0;
    Double_t graph_max = 0.0;
    if (!graph_x_range(graph.get(), graph_min, graph_max)) {
      continue;
    }
    const Double_t margin = std::max(0.1 * (graph_max - graph_min), 1.0 / 60.0);
    graph->GetXaxis()->SetLimits(graph_min - margin, graph_max + margin);
  }

  for (std::size_t i = 0; i < hist_tofs_.size() && i < tof_entry_labels_.size(); ++i) {
    update_entry_label(tof_entry_labels_[i].get(), hist_tofs_[i].get());
  }
  for (std::size_t i = 0; i < hist_tofs_us_.size() && i < tof_us_entry_labels_.size(); ++i) {
    update_entry_label(tof_us_entry_labels_[i].get(), hist_tofs_us_[i].get());
  }
  for (std::size_t i = 0; i < hist_tof_groups_.size() && i < tof_group_entry_labels_.size(); ++i) {
    update_entry_label(tof_group_entry_labels_[i].get(), hist_tof_groups_[i].get());
  }
  for (std::size_t i = 0; i < hist_tof_groups_us_.size() && i < tof_group_us_entry_labels_.size(); ++i) {
    update_entry_label(tof_group_us_entry_labels_[i].get(), hist_tof_groups_us_[i].get());
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
  if (!parse_overview_spec(spec, error_text)) {
    return false;
  }

  uint64_t exclude_reconfig = 1;
  if (!spec.ReadU64("exclude_reconfig", 1, exclude_reconfig, error_text)) {
    return false;
  }

  out_analysis = std::make_unique<Kc705TofOverviewAnalysis>(exclude_reconfig != 0);
  error_text.clear();
  return true;
}

const IMonitorRealtimeAnalysisFactory& GetKc705TofOverviewAnalysisFactory() {
  static Kc705TofOverviewAnalysisFactory factory;
  return factory;
}

REGISTER_ANALYSIS(GetKc705TofOverviewAnalysisFactory);
