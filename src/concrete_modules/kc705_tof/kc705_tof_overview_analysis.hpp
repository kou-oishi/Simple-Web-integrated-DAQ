#pragma once

#include <array>
#include <chrono>
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
  template <typename T>
  using StageArray = std::array<std::unique_ptr<T>, 3>;

  struct ExclusionRange {
    Double_t start_unix = 0.0;
    Double_t end_unix = 0.0;
  };

  struct HitPairState {
    std::array<Double_t, Kc705TofNamedChannelCount()> last_timestamp_sec_{};
    std::array<bool, Kc705TofNamedChannelCount()> has_last_timestamp_sec_{};
  };

  struct DeferredNamedHit {
    std::size_t channel_index = 0;
    std::size_t board_index = 0;
    Double_t timestamp_sec = 0.0;
    Double_t time_in_minutes = 0.0;
    Double_t event_time_ms = 0.0;
    bool has_tof_reference = false;
    Double_t first_periodic_time_ms = 0.0;
    bool has_prev_filtered = false;
    Double_t left_gap_sec = 0.0;
    bool left_gap_large = false;
    bool is_live = false;
    std::chrono::steady_clock::time_point decision_deadline{};
  };

  bool Event(const Kc705TofEvent& Event, const DecodedMessage& message, std::string& error_text) override;
  bool Event(const Kc705TofEvent& Event, std::string& error_text) override;
  bool LoadExcludedTimeRanges(std::string& error_text);
  bool IsExcludedByTimestamp(const Kc705TofEvent& event) const;
  void ProcessHitDelta(HitPairState& state,
                       std::vector<std::unique_ptr<TH1D>>& histograms,
                       std::size_t channel_index,
                       Double_t timestamp_sec);
  void ProcessCleanCandidate(const DeferredNamedHit& current);
  void FillCleanNamedHit(const DeferredNamedHit& hit);
  void FlushExpiredCleanCandidates(bool flush_all);

  Double_t daq_start_time_[2] = {-1, -1};
  Double_t min_time_ = 1e100, max_time_ = -1e100;
  bool enable_exclusion_filter_ = true;
  Double_t clean_cluster_sec_ = 1.0;
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
  std::unique_ptr<TCanvas> canvas_hit_deltas_;
  std::unique_ptr<TCanvas> canvas_tof_groups_;
  std::unique_ptr<TCanvas> canvas_tof_groups_us_;
  StageArray<TH1D> hist_board_stages_{};
  StageArray<TH1D> hist_channel_stages_{};
  StageArray<TH1D> hist_channel_named_stages_{};
  std::vector<std::unique_ptr<TH1D>> hist_tdcs_;
  std::vector<std::unique_ptr<TH1D>> hist_tdcs_filtered_;
  std::vector<std::unique_ptr<TH1D>> hist_tdcs_clean_;
  std::vector<std::unique_ptr<TH1D>> hist_tofs_;
  std::vector<std::unique_ptr<TH1D>> hist_tofs_filtered_;
  std::vector<std::unique_ptr<TH1D>> hist_tofs_clean_;
  std::vector<std::unique_ptr<TH1D>> hist_tofs_us_;
  std::vector<std::unique_ptr<TH1D>> hist_tofs_us_filtered_;
  std::vector<std::unique_ptr<TH1D>> hist_tofs_us_clean_;
  std::vector<std::unique_ptr<TH1D>> hist_hit_deltas_all_;
  std::vector<std::unique_ptr<TH1D>> hist_hit_deltas_filtered_;
  std::vector<std::unique_ptr<TH1D>> hist_hit_deltas_clean_;
  std::vector<std::unique_ptr<TH1D>> hist_tof_groups_;
  std::vector<std::unique_ptr<TH1D>> hist_tof_groups_filtered_;
  std::vector<std::unique_ptr<TH1D>> hist_tof_groups_clean_;
  std::vector<std::unique_ptr<TH1D>> hist_tof_groups_us_;
  std::vector<std::unique_ptr<TH1D>> hist_tof_groups_us_filtered_;
  std::vector<std::unique_ptr<TH1D>> hist_tof_groups_us_clean_;
  StageArray<TLatex> board_entry_labels_{};
  StageArray<TLatex> channel_entry_labels_{};
  StageArray<TLatex> channel_named_entry_labels_{};
  std::vector<std::unique_ptr<TLatex>> tdc_all_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tdc_filtered_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tdc_clean_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_filtered_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_clean_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_us_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_us_filtered_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_us_clean_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> hit_delta_all_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> hit_delta_filtered_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> hit_delta_clean_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_group_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_group_filtered_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_group_clean_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_group_us_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_group_us_filtered_entry_labels_;
  std::vector<std::unique_ptr<TLatex>> tof_group_us_clean_entry_labels_;
  std::array<std::unique_ptr<TGraph>, Kc705TofPeriodicChannelCount()> periodic_rate_graphs_;
  std::array<std::deque<Double_t>, Kc705TofPeriodicChannelCount()> periodic_event_times_;
  std::array<Double_t, 2> last_periodic_time_ms_ = {0.0, 0.0};
  std::array<Double_t, 2> first_periodic_time_ms_ = {0.0, 0.0};
  std::array<bool, 2> has_last_periodic_time_ms_ = {false, false};
  std::array<bool, 2> has_first_periodic_time_ms_ = {false, false};
  HitPairState hit_pair_state_{};
  HitPairState hit_pair_state_filtered_{};
  std::array<std::optional<DeferredNamedHit>, Kc705TofNamedChannelCount()> deferred_clean_hits_{};
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
