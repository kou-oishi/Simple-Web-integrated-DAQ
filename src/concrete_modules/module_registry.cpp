#include "concrete_modules/module_registry.hpp"

#include <string>
#include <vector>

#if defined(SIMPLEDAQ_ENABLE_FRONTEND_REGISTRY) && SIMPLEDAQ_ENABLE_FRONTEND_REGISTRY
#include "concrete_modules/kc705_tof/kc705_tof_frontend.hpp"
#endif
#if defined(SIMPLEDAQ_ENABLE_DECODER_REGISTRY) && SIMPLEDAQ_ENABLE_DECODER_REGISTRY
#include "concrete_modules/kc705_tof/kc705_tof_decoder.hpp"
#endif

namespace {

#if defined(SIMPLEDAQ_ENABLE_FRONTEND_REGISTRY) && SIMPLEDAQ_ENABLE_FRONTEND_REGISTRY
const Kc705TofFrontend k_kc705_tof_frontend;
#endif

const IDeviceFrontend* Kc705TofFrontendPtr() {
#if defined(SIMPLEDAQ_ENABLE_FRONTEND_REGISTRY) && SIMPLEDAQ_ENABLE_FRONTEND_REGISTRY
  return &k_kc705_tof_frontend;
#else
  return nullptr;
#endif
}

#if defined(SIMPLEDAQ_ENABLE_DECODER_REGISTRY) && SIMPLEDAQ_ENABLE_DECODER_REGISTRY
const IMonitorDecoderFactory* Kc705TofDecoderFactoryPtr() {
  return &GetKc705TofDecoderFactory();
}
#else
const IMonitorDecoderFactory* Kc705TofDecoderFactoryPtr() {
  return nullptr;
}
#endif

}  // namespace

const std::vector<ConcreteModuleRegistration>& GetConcreteModuleRegistry() {
  static const std::vector<ConcreteModuleRegistration> kModules = {
      {.id = "kc705_tof", .frontend = Kc705TofFrontendPtr(), .decoder_factory = Kc705TofDecoderFactoryPtr()},
  };
  return kModules;
}

const IDeviceFrontend* FindDeviceFrontend(const std::string& frontend_id) {
  const auto& modules = GetConcreteModuleRegistry();
  for (const auto& module : modules) {
    if (module.frontend != nullptr && frontend_id == module.id) {
      return module.frontend;
    }
  }
  return nullptr;
}

std::vector<std::string> ListDeviceFrontendIds() {
  const auto& modules = GetConcreteModuleRegistry();
  std::vector<std::string> ids;
  ids.reserve(modules.size());
  for (const auto& module : modules) {
    if (module.frontend != nullptr) {
      ids.emplace_back(module.id);
    }
  }
  return ids;
}

const IMonitorDecoderFactory* FindMonitorDecoderFactory(const std::string& name) {
  const auto& modules = GetConcreteModuleRegistry();
  for (const auto& module : modules) {
    if (module.decoder_factory != nullptr && name == module.decoder_factory->name()) {
      return module.decoder_factory;
    }
  }
  return nullptr;
}

std::vector<std::string> ListMonitorDecoderFactories() {
  const auto& modules = GetConcreteModuleRegistry();
  std::vector<std::string> names;
  names.reserve(modules.size());
  for (const auto& module : modules) {
    if (module.decoder_factory != nullptr) {
      names.emplace_back(module.decoder_factory->name());
    }
  }
  return names;
}
