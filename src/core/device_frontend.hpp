#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/daq_types.hpp"
#include "core/data_validator.hpp"
#include "core/device_driver.hpp"

enum class DeviceFrontendFieldType {
  kInteger,
  kString,
  kIpv4,
};

struct DeviceFrontendFieldSchema {
  std::string name;
  DeviceFrontendFieldType type = DeviceFrontendFieldType::kString;
  bool required = true;
  bool has_min = false;
  int64_t min_value = 0;
  bool has_max = false;
  int64_t max_value = 0;
  std::string description;
};

struct DeviceFrontendSchema {
  std::string spec_format;
  std::string spec_template;
  std::vector<DeviceFrontendFieldSchema> fields;
};

class IDeviceFrontend {
 public:
  virtual ~IDeviceFrontend() = default;

  virtual const char* Id() const = 0;
  virtual DeviceFrontendSchema DescribeDeviceSpec() const = 0;

  // Parse frontend-specific device spec after '=' and populate DeviceSpec.
  virtual bool ParseDeviceSpec(const std::string& spec_after_equals,
                                 DeviceSpec& out_device,
                                 std::string& error_message) const = 0;

  virtual std::unique_ptr<IDeviceDriver> CreateDriver(const DeviceSpec& device) const = 0;
  virtual std::unique_ptr<IDataValidator> CreateValidator(const DeviceSpec& device) const = 0;
};
