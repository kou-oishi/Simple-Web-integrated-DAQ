#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "concrete_modules/kc705_tof/kc705_tof_types.hpp"
#include "monitor/realtime_analysis.hpp"

class TH1D;
class TCanvas;
class TGraph;
class TLatex;

class Kc705TofOverviewAnalysis final : public TypedRealtimeAnalysis<Kc705TofEvent> {
 public:
  explicit Kc705TofOverviewAnalysis(bool enable_exclusion_filter = true)
      : enable_exclusion_filter_(enable_exclusion_filter) {}

  bool Initialise(std::string& error_text) override;
  bool BeginOfRun(uint32_t run_number, std::string& error_text) override;
 bool EndOfRun(uint32_t run_number, std::string& error_text) override;
  bool UpdateDrawables(std::string& error_text) override;
  bool Finalise(std::string& error_text) override;

 private:
  struct ExclusionRange {
    Double_t start_unix = 0.0;
    Double_t end_unix = 0.0;
  };

  bool Event(const Kc705TofEvent& Event, std::string& error_text) override;
  bool LoadExcludedTimeRanges(std::string& error_text);
  bool IsExcludedByTimestamp(const Kc705TofEvent& event) const;

  Double_t daq_start_time_[2] = {-1, -1};
  Double_t min_time_ = 1e100, max_time_ = -1e100;
  bool enable_exclusion_filter_ = true;
  std::array<std::vector<ExclusionRange>, Kc705TofNamedChannelCount()> excluded_time_ranges_;
  uint64_t excluded_event_count_ = 0;
  std::array<uint64_t, Kc705TofNamedChannelCount()> excluded_event_count_by_channel_{};
  
  std::unique_ptr<TCanvas> canvas_board_;
  std::unique_ptr<TCanvas> canvas_channel_;
  std::unique_ptr<TCanvas> canvas_channel_named_;
  std::unique_ptr<TCanvas> canvas_periodic_rate_trend_;
  std::unique_ptr<TCanvas> canvas_tdcs_;
  std::unique_ptr<TCanvas> canvas_tofs_;
  std::unique_ptr<TCanvas> canvas_tofs_us_;
  std::unique_ptr<TCanvas> canvas_tof_groups_;
  std::unique_ptr<TCanvas> canvas_tof_groups_us_;
  std::unique_ptr<TH1D> hist_board_;
  std::unique_ptr<TH1D> hist_channel_;
  std::unique_ptr<TH1D> hist_channel_named_;
  std::vector<std::unique_ptr<TH1D>> hist_tdcs_;
  std::vector<std::unique_ptr<TH1D>> hist_tofs_;
  std::vector<std::unique_ptr<TH1D>> hist_tofs_us_;
  std::vector<std::unique_ptr<TH1D>> hist_tof_groups_;
  std::vector<std::unique_ptr<TH1D>> hist_tof_groups_us_;
  std::vector<std::unique_ptr<TLatex>> tof_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_us_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_group_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_group_us_entry_labels_;
  std::array<std::unique_ptr<TGraph>, Kc705TofPeriodicChannelCount()> periodic_rate_graphs_;
  std::array<std::deque<Double_t>, Kc705TofPeriodicChannelCount()> periodic_event_times_;
  std::array<Double_t, 2> last_periodic_time_ms_ = {0.0, 0.0};
  std::array<Double_t, 2> first_periodic_time_ms_ = {0.0, 0.0};
  std::array<bool, 2> has_last_periodic_time_ms_ = {false, false};
  std::array<bool, 2> has_first_periodic_time_ms_ = {false, false};
};

class Kc705TofOverviewAnalysisFactory final : public IMonitorRealtimeAnalysisFactory {
 public:
  const char* Name() const override;
  const char* Title() const override;
  const char* ExpectedDecoder() const override;
  bool Create(const ParsedAnalysisSpec& spec,
              std::unique_ptr<IRealtimeAnalysis>& out_analysis,
              std::string& error_text) const override;
};

const IMonitorRealtimeAnalysisFactory& GetKc705TofOverviewAnalysisFactory();
