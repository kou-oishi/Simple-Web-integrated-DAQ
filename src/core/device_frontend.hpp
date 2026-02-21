#pragma once

#include <memory>
#include <string>

#include "core/daq_types.hpp"
#include "core/data_validator.hpp"
#include "core/device_driver.hpp"

class IDeviceFrontend {
 public:
  virtual ~IDeviceFrontend() = default;

  virtual const char* Id() const = 0;

  // Parse frontend-specific device spec after '=' and populate DeviceSpec.
  virtual bool ParseDeviceSpec(const std::string& spec_after_equals,
                                 DeviceSpec& out_device,
                                 std::string& error_message) const = 0;

  virtual std::unique_ptr<IDeviceDriver> CreateDriver(const DeviceSpec& device) const = 0;
  virtual std::unique_ptr<IDataValidator> CreateValidator(const DeviceSpec& device) const = 0;
};
