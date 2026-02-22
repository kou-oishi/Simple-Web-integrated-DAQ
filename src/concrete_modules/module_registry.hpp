#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "core/device_frontend.hpp"
#include "monitor/decoder.hpp"
#include "monitor/realtime_analysis.hpp"

class IMonitorDecoderFactory {
 public:
  virtual ~IMonitorDecoderFactory() = default;

  virtual const char* Name() const = 0;
  virtual const char* Title() const { return Name(); }
  virtual bool Create(const std::string& spec,
                      std::unique_ptr<IDecoder>& out_decoder,
                      std::size_t& out_frame_size,
                      std::string& error_text) const = 0;
};

bool RegisterDeviceFrontend(std::unique_ptr<IDeviceFrontend> frontend);
bool RegisterMonitorDecoderFactory(const IMonitorDecoderFactory* factory);
bool RegisterRealtimeAnalysisFactory(const IMonitorRealtimeAnalysisFactory* factory);

const IDeviceFrontend* FindDeviceFrontend(const std::string& frontend_id);
std::vector<std::string> ListDeviceFrontendIds();

const IMonitorDecoderFactory* FindMonitorDecoderFactory(const std::string& Name);
std::vector<std::string> ListMonitorDecoderFactories();

const IMonitorRealtimeAnalysisFactory* FindRealtimeAnalysisFactory(const std::string& Name);
std::vector<std::string> ListRealtimeAnalysisFactories();

#define SIMPLEDAQ_CONCAT_IMPL_(a, b) a##b
#define SIMPLEDAQ_CONCAT_(a, b) SIMPLEDAQ_CONCAT_IMPL_(a, b)

#if defined(__clang__) || defined(__GNUC__)
#define SIMPLEDAQ_USED_ __attribute__((used))
#else
#define SIMPLEDAQ_USED_
#endif

#define REGISTER_FRONTEND(FRONTEND_TYPE)                                                                  \
  namespace {                                                                                              \
  SIMPLEDAQ_USED_ const bool SIMPLEDAQ_CONCAT_(kRegisterFrontend_, __COUNTER__) =                         \
      RegisterDeviceFrontend(std::make_unique<FRONTEND_TYPE>());                                          \
  }

#define REGISTER_DECODER(FACTORY_GETTER_FN)                                                                \
  namespace {                                                                                               \
  SIMPLEDAQ_USED_ const bool SIMPLEDAQ_CONCAT_(kRegisterDecoder_, __COUNTER__) =                           \
      RegisterMonitorDecoderFactory(&(FACTORY_GETTER_FN()));                                                \
  }

#define REGISTER_ANALYSIS(FACTORY_GETTER_FN)                                                               \
  namespace {                                                                                              \
  SIMPLEDAQ_USED_ const bool SIMPLEDAQ_CONCAT_(kRegisterAnalysis_, __COUNTER__) =                         \
      RegisterRealtimeAnalysisFactory(&(FACTORY_GETTER_FN()));                                            \
  }
