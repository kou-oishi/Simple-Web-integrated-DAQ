#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "concrete_modules/kc705_tof/kc705_tof_types.hpp"
#include "monitor/realtime_analysis.hpp"

class TH1D;
class TGraph;
class TCanvas;

class Kc705TofOverviewAnalysis final : public TypedRealtimeAnalysis<Kc705TofEvent> {
 public:
  explicit Kc705TofOverviewAnalysis(std::size_t trend_points);

  bool Initialise(std::string& error_text) override;
  bool Finalise(std::string& error_text) override;

 private:
  bool Event(const Kc705TofEvent& Event, std::string& error_text) override;

  std::size_t trend_points_ = 1000;

  std::unique_ptr<TCanvas> canvas_board_;
  std::unique_ptr<TCanvas> canvas_channel_;
  std::unique_ptr<TCanvas> canvas_trend_;
  std::unique_ptr<TH1D> hist_board_;
  std::unique_ptr<TH1D> hist_channel_;
  std::unique_ptr<TGraph> graph_trend_;
};

class Kc705TofOverviewAnalysisFactory final : public IMonitorRealtimeAnalysisFactory {
 public:
  const char* Name() const override;
  const char* ExpectedDecoder() const override;
  bool Create(const ParsedAnalysisSpec& spec,
              std::unique_ptr<IRealtimeAnalysis>& out_analysis,
              std::string& error_text) const override;
};

const IMonitorRealtimeAnalysisFactory& GetKc705TofOverviewAnalysisFactory();
