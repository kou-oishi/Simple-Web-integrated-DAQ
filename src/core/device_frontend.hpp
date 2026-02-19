#pragma once

#include <memory>
#include <string>

#include "core/daq_types.hpp"
#include "core/data_validator.hpp"
#include "core/device_driver.hpp"

class IDeviceFrontend {
 public:
  virtual ~IDeviceFrontend() = default;

  virtual const char* id() const = 0;

  // Parse frontend-specific device spec after '=' and populate DeviceSpec.
  virtual bool parse_device_spec(const std::string& spec_after_equals,
                                 DeviceSpec& out_device,
                                 std::string& error_message) const = 0;

  virtual std::unique_ptr<IDeviceDriver> create_driver(const DeviceSpec& device) const = 0;
  virtual std::unique_ptr<IDataValidator> create_validator(const DeviceSpec& device) const = 0;
};
