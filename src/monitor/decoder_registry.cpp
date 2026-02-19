#include "monitor/decoder_registry.hpp"

#include <array>

#include "decoders/kc705_tof_decoder.hpp"

namespace {

const std::array<const IMonitorDecoderFactory*, 1> kFactories = {
    &GetKc705TofDecoderFactory(),
};

}  // namespace

const IMonitorDecoderFactory* FindMonitorDecoderFactory(const std::string& name) {
  for (const auto* f : kFactories) {
    if (name == f->name()) {
      return f;
    }
  }
  return nullptr;
}

std::vector<std::string> ListMonitorDecoderFactories() {
  std::vector<std::string> names;
  names.reserve(kFactories.size());
  for (const auto* f : kFactories) {
    names.emplace_back(f->name());
  }
  return names;
}
