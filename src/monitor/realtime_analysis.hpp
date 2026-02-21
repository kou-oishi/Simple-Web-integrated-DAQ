#pragma once

#include <any>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "monitor/decoded_message.hpp"

class TCanvas;
class TObject;
class TVirtualPad;

class IRealtimeDisplayRegistry {
 public:
  virtual ~IRealtimeDisplayRegistry() = default;

  virtual bool RegisterCanvas(TCanvas* canvas, std::string& error_text) = 0;
  virtual bool RegisterDrawable(TVirtualPad* pad,
                                 TObject* object,
                                 const char* draw_option,
                                 std::string& error_text) = 0;
};

class IRealtimeAnalysis {
 public:
  virtual ~IRealtimeAnalysis() = default;

  virtual bool Initialise(std::string& error_text) = 0;
  virtual bool Event(const DecodedMessage& message, std::string& error_text) = 0;
  virtual bool Finalise(std::string& error_text) = 0;

  void SetDisplayRegistry(IRealtimeDisplayRegistry* registry) { display_registry_ = registry; }

 protected:
  bool RegisterCanvas(TCanvas* canvas, std::string& error_text) const;
  bool RegisterDrawable(TVirtualPad* pad, TObject* object, const char* draw_option, std::string& error_text) const;

 private:
  IRealtimeDisplayRegistry* display_registry_ = nullptr;
};

template <typename TPayload>
class TypedRealtimeAnalysis : public IRealtimeAnalysis {
 public:
  bool Event(const DecodedMessage& message, std::string& error_text) final {
    error_text.clear();
    const auto* payload = std::any_cast<TPayload>(&message.payload);
    if (payload == nullptr) {
      error_text = "realtime analysis received unexpected decoded payload type";
      return false;
    }
    return Event(*payload, error_text);
  }

 protected:
  virtual bool Event(const TPayload& payload, std::string& error_text) = 0;
};

bool ParseKeyValueSpec(const std::string& spec,
                       std::vector<std::pair<std::string, std::string>>& out_pairs,
                       std::string& error_text);
bool ParseUnsignedU64(const std::string& text, uint64_t& out);
bool ParseUnsignedSize(const std::string& text, std::size_t& out);

class ParsedAnalysisSpec {
 public:
  bool Parse(const std::string& spec, std::string& error_text);
  bool ReadU64(const char* key, uint64_t default_value, uint64_t& out, std::string& error_text) const;
  bool ReadSize(const char* key, std::size_t default_value, std::size_t& out, std::string& error_text) const;
  bool RejectUnknown(std::initializer_list<const char*> allowed_keys, std::string& error_text) const;

 private:
  std::vector<std::pair<std::string, std::string>> kv_pairs_;
};

class IMonitorRealtimeAnalysisFactory {
 public:
  virtual ~IMonitorRealtimeAnalysisFactory() = default;

  virtual const char* Name() const = 0;
  virtual const char* ExpectedDecoder() const = 0;
  virtual bool Create(const ParsedAnalysisSpec& spec,
                      std::unique_ptr<IRealtimeAnalysis>& out_analysis,
                      std::string& error_text) const = 0;
};
