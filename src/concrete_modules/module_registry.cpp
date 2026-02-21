#include "concrete_modules/module_registry.hpp"

#include <algorithm>

namespace {

std::vector<std::unique_ptr<IDeviceFrontend>>& frontend_registry() {
  static std::vector<std::unique_ptr<IDeviceFrontend>> registry;
  return registry;
}

std::vector<const IMonitorDecoderFactory*>& decoder_registry() {
  static std::vector<const IMonitorDecoderFactory*> registry;
  return registry;
}

std::vector<const IMonitorRealtimeAnalysisFactory*>& analysis_registry() {
  static std::vector<const IMonitorRealtimeAnalysisFactory*> registry;
  return registry;
}

}  // namespace

bool RegisterDeviceFrontend(std::unique_ptr<IDeviceFrontend> frontend) {
  if (frontend == nullptr) {
    return false;
  }
  auto& registry = frontend_registry();
  const char* Id = frontend->Id();
  const auto exists = std::any_of(registry.begin(), registry.end(), [&](const std::unique_ptr<IDeviceFrontend>& item) {
    return std::string(item->Id()) == Id;
  });
  if (exists) {
    return false;
  }
  registry.push_back(std::move(frontend));
  return true;
}

bool RegisterMonitorDecoderFactory(const IMonitorDecoderFactory* factory) {
  if (factory == nullptr || factory->Name() == nullptr) {
    return false;
  }
  auto& registry = decoder_registry();
  const auto exists = std::any_of(registry.begin(), registry.end(),
                                  [&](const IMonitorDecoderFactory* item) { return std::string(item->Name()) == factory->Name(); });
  if (exists) {
    return false;
  }
  registry.push_back(factory);
  return true;
}

bool RegisterRealtimeAnalysisFactory(const IMonitorRealtimeAnalysisFactory* factory) {
  if (factory == nullptr || factory->Name() == nullptr) {
    return false;
  }
  auto& registry = analysis_registry();
  const auto exists = std::any_of(registry.begin(), registry.end(), [&](const IMonitorRealtimeAnalysisFactory* item) {
    return std::string(item->Name()) == factory->Name();
  });
  if (exists) {
    return false;
  }
  registry.push_back(factory);
  return true;
}

const IDeviceFrontend* FindDeviceFrontend(const std::string& frontend_id) {
  const auto& registry = frontend_registry();
  for (const auto& frontend : registry) {
    if (frontend != nullptr && frontend_id == frontend->Id()) {
      return frontend.get();
    }
  }
  return nullptr;
}

std::vector<std::string> ListDeviceFrontendIds() {
  const auto& registry = frontend_registry();
  std::vector<std::string> ids;
  ids.reserve(registry.size());
  for (const auto& frontend : registry) {
    if (frontend != nullptr) {
      ids.emplace_back(frontend->Id());
    }
  }
  return ids;
}

const IMonitorDecoderFactory* FindMonitorDecoderFactory(const std::string& Name) {
  const auto& registry = decoder_registry();
  for (const auto* factory : registry) {
    if (factory != nullptr && Name == factory->Name()) {
      return factory;
    }
  }
  return nullptr;
}

std::vector<std::string> ListMonitorDecoderFactories() {
  const auto& registry = decoder_registry();
  std::vector<std::string> names;
  names.reserve(registry.size());
  for (const auto* factory : registry) {
    if (factory != nullptr) {
      names.emplace_back(factory->Name());
    }
  }
  return names;
}

const IMonitorRealtimeAnalysisFactory* FindRealtimeAnalysisFactory(const std::string& Name) {
  const auto& registry = analysis_registry();
  for (const auto* factory : registry) {
    if (factory != nullptr && Name == factory->Name()) {
      return factory;
    }
  }
  return nullptr;
}

std::vector<std::string> ListRealtimeAnalysisFactories() {
  const auto& registry = analysis_registry();
  std::vector<std::string> names;
  names.reserve(registry.size());
  for (const auto* factory : registry) {
    if (factory != nullptr) {
      names.emplace_back(factory->Name());
    }
  }
  return names;
}
