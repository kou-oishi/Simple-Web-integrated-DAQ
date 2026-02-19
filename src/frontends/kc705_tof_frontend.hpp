#pragma once

#include "core/device_frontend.hpp"

class Kc705TofFrontend : public IDeviceFrontend {
 public:
  const char* id() const override { return "kc705_tof"; }

  bool parse_device_spec(const std::string& spec_after_equals,
                         DeviceSpec& out_device,
                         std::string& error_message) const override;

  std::unique_ptr<IDeviceDriver> create_driver(const DeviceSpec& device) const override;
  std::unique_ptr<IDataValidator> create_validator(const DeviceSpec& device) const override;
};
