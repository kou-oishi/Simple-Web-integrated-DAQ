#include "concrete_modules/kc705_tof/kc705_tof_frontend.hpp"

#include <limits>
#include <memory>
#include <string>

#include "drivers/tcp_device_driver.hpp"
#include "concrete_modules/module_registry.hpp"
#include "concrete_modules/kc705_tof/kc705_tof_validator.hpp"

namespace {

bool parse_u32(const std::string& s, uint32_t& out) {
  try {
    size_t pos = 0;
    unsigned long v = std::stoul(s, &pos, 10);
    if (pos != s.size() || v > std::numeric_limits<uint32_t>::max()) {
      return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

bool Kc705TofFrontend::parse_device_spec(const std::string& spec_after_equals,
                                         DeviceSpec& out_device,
                                         std::string& error_message) const {
  const size_t at = spec_after_equals.find('@');
  const size_t colon = spec_after_equals.rfind(':');
  if (at == std::string::npos || colon == std::string::npos || at == 0 || colon <= at + 1 || colon + 1 >= spec_after_equals.size()) {
    error_message = "expected <board_id@host:port>";
    return false;
  }

  uint32_t board_u32 = 0;
  const std::string board_str = spec_after_equals.substr(0, at);
  if (!parse_u32(board_str, board_u32) || board_u32 > 7) {
    error_message = "invalid board_id (expected 0..7)";
    return false;
  }
  const std::string host = spec_after_equals.substr(at + 1, colon - (at + 1));

  uint32_t port_u32 = 0;
  const std::string port_str = spec_after_equals.substr(colon + 1);
  if (!parse_u32(port_str, port_u32) || port_u32 == 0 || port_u32 > 65535) {
    error_message = "invalid TCP port in <board_id@host:port>";
    return false;
  }

  out_device.frontend = id();
  out_device.board_id = static_cast<uint8_t>(board_u32);
  out_device.host = host;
  out_device.port = static_cast<uint16_t>(port_u32);
  return true;
}

std::unique_ptr<IDeviceDriver> Kc705TofFrontend::create_driver(const DeviceSpec& device) const {
  return std::make_unique<TcpDeviceDriver>(device.host, device.port, TcpDeviceDriver::EndianMode::kRawBytes);
}

std::unique_ptr<IDataValidator> Kc705TofFrontend::create_validator(const DeviceSpec& device) const {
  return std::make_unique<Kc705TofValidator>(device.board_id);
}

REGISTER_FRONTEND(Kc705TofFrontend);
