#pragma once

#include "core/device_frontend.hpp"

class Kc705TofFrontend : public IDeviceFrontend {
 public:
  const char* Id() const override { return "kc705_tof"; }

  bool ParseDeviceSpec(const std::string& spec_after_equals,
                         DeviceSpec& out_device,
                         std::string& error_message) const override;

  std::unique_ptr<IDeviceDriver> CreateDriver(const DeviceSpec& device) const override;
  std::unique_ptr<IDataValidator> CreateValidator(const DeviceSpec& device) const override;
};
