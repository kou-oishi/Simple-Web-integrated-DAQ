#include "core/device_frontend_registry.hpp"

#include <vector>

#include "concrete_modules/kc705_tof/kc705_tof_frontend.hpp"

namespace {

const Kc705TofFrontend k_kc705_tof_frontend;

}  // namespace

const IDeviceFrontend* FindDeviceFrontend(const std::string& frontend_id) {
  if (frontend_id == k_kc705_tof_frontend.id()) {
    return &k_kc705_tof_frontend;
  }
  return nullptr;
}

std::vector<std::string> ListDeviceFrontendIds() {
  return {k_kc705_tof_frontend.id()};
}
