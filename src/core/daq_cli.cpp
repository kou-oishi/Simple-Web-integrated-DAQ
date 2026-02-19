#include "core/daq_cli.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#include "core/device_frontend_registry.hpp"

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

std::string supported_frontends_text() {
  const auto ids = ListDeviceFrontendIds();
  std::ostringstream oss;
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i != 0) {
      oss << ", ";
    }
    oss << ids[i];
  }
  return oss.str();
}

}  // namespace

void PrintDaqUsage(const char* prog) {
  std::cerr << "Usage: " << prog
            << " --output-dir <dir> --device <frontend>=<spec> [--device <...>]"
            << " [--run-start <>=0>] [--events-per-file <>=1>]"
            << " [--reconnect-ms <>=1>] [--read-timeout-ms <>=1>] [--duration-sec <>=1>]\n";
  std::cerr << "Available frontends: " << supported_frontends_text() << "\n";
  std::cerr << "Example: --device kc705_tof=1@127.0.0.2:9101\n";
}

bool ParseDaqArgs(int argc, char** argv, DaqConfig& cfg) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto require_value = [&](const char* opt) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << opt << "\n";
        return nullptr;
      }
      return argv[++i];
    };

    if (arg == "--output-dir" || arg == "--out-dir" || arg == "--out") {
      const char* val = require_value(arg.c_str());
      if (!val) return false;
      cfg.output_dir = val;
    } else if (arg == "--device") {
      const char* val = require_value("--device");
      if (!val) return false;

      const std::string device_arg = val;
      const size_t eq = device_arg.find('=');
      if (eq == std::string::npos || eq == 0 || eq + 1 >= device_arg.size()) {
        std::cerr << "Invalid --device: " << device_arg << " (expected frontend=<spec>)\n";
        return false;
      }

      const std::string frontend_id = device_arg.substr(0, eq);
      const std::string frontend_spec = device_arg.substr(eq + 1);
      const IDeviceFrontend* frontend = FindDeviceFrontend(frontend_id);
      if (frontend == nullptr) {
        std::cerr << "Unknown frontend '" << frontend_id << "'. Supported: " << supported_frontends_text() << "\n";
        return false;
      }

      DeviceSpec spec;
      std::string parse_error;
      if (!frontend->parse_device_spec(frontend_spec, spec, parse_error)) {
        std::cerr << "Invalid --device for frontend '" << frontend_id << "': " << parse_error << "\n";
        return false;
      }
      cfg.devices.push_back(spec);
    } else if (arg == "--run-start") {
      const char* val = require_value("--run-start");
      if (!val) return false;
      uint32_t tmp = 0;
      if (!parse_u32(val, tmp)) {
        std::cerr << "Invalid --run-start: " << val << " (expected >=0)\n";
        return false;
      }
      cfg.run_start = tmp;
    } else if (arg == "--events-per-file") {
      const char* val = require_value("--events-per-file");
      if (!val) return false;
      uint32_t tmp = 0;
      if (!parse_u32(val, tmp) || tmp == 0) {
        std::cerr << "Invalid --events-per-file: " << val << " (expected >=1)\n";
        return false;
      }
      cfg.events_per_file = tmp;
    } else if (arg == "--reconnect-ms") {
      const char* val = require_value("--reconnect-ms");
      if (!val) return false;
      uint32_t tmp = 0;
      if (!parse_u32(val, tmp) || tmp == 0) {
        std::cerr << "Invalid --reconnect-ms: " << val << " (expected >=1)\n";
        return false;
      }
      cfg.reconnect_ms = tmp;
    } else if (arg == "--read-timeout-ms") {
      const char* val = require_value("--read-timeout-ms");
      if (!val) return false;
      uint32_t tmp = 0;
      if (!parse_u32(val, tmp) || tmp == 0) {
        std::cerr << "Invalid --read-timeout-ms: " << val << " (expected >=1)\n";
        return false;
      }
      cfg.read_timeout_ms = tmp;
    } else if (arg == "--duration-sec") {
      const char* val = require_value("--duration-sec");
      if (!val) return false;
      uint32_t tmp = 0;
      if (!parse_u32(val, tmp) || tmp == 0) {
        std::cerr << "Invalid --duration-sec: " << val << " (expected >=1)\n";
        return false;
      }
      cfg.duration_sec = tmp;
    } else if (arg == "--help" || arg == "-h") {
      PrintDaqUsage(argv[0]);
      std::exit(0);
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      return false;
    }
  }

  if (cfg.output_dir.empty()) {
    std::cerr << "--output-dir is required\n";
    return false;
  }
  if (cfg.devices.empty()) {
    std::cerr << "At least one --device is required\n";
    return false;
  }

  return true;
}
