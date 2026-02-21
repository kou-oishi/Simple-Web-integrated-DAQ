#include "core/daq_cli.hpp"

#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#include "concrete_modules/module_registry.hpp"

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
            << " [--Run-start <>=0>] [--events-per-file <>=1>]"
            << " [--reconnect-ms <>=1>] [--read-timeout-ms <>=1>] [--duration-sec <>=1>]"
            << " [--startup-connect-timeout-sec <>=1>] [--reconnect-failure-timeout-sec <>=1>]\n";
  std::cerr << "Options:\n";
  std::cerr << "  -o, --output-dir <dir>         Output directory for Run files (required)\n";
  std::cerr << "  -d, --device <frontend>=<spec> Input device spec; repeatable\n";
  std::cerr << "  -r, --Run-start <n>            Starting Run number (>= 0)\n";
  std::cerr << "  -e, --events-per-file <n>      Events per file (>= 1)\n";
  std::cerr << "  -m, --comment <text>           Run comment recorded in MySQL run log\n";
  std::cerr << "  -c, --reconnect-ms <ms>        Reconnect interval in milliseconds (>= 1)\n";
  std::cerr << "  -t, --read-timeout-ms <ms>     Read timeout in milliseconds (>= 1)\n";
  std::cerr << "  -u, --duration-sec <sec>       Run duration in seconds (>= 1, 0 means unlimited)\n";
  std::cerr << "  -x, --startup-connect-timeout-sec <sec>  Startup connect timeout in seconds (>= 1)\n";
  std::cerr << "  -f, --reconnect-failure-timeout-sec <sec> Reconnect-failure timeout in seconds (>= 1)\n";
  std::cerr << "  -h, --help                     Show this help\n";
  std::cerr << "Available frontends: " << supported_frontends_text() << "\n";
  std::cerr << "Example: --device kc705_tof=1@127.0.0.2:9101\n";
}

bool ParseDaqArgs(int argc, char** argv, DaqConfig& cfg) {
  static constexpr option kLongOpts[] = {
      {"output-dir", required_argument, nullptr, 'o'},
      {"device", required_argument, nullptr, 'd'},
      {"Run-start", required_argument, nullptr, 'r'},
      {"events-per-file", required_argument, nullptr, 'e'},
      {"comment", required_argument, nullptr, 'm'},
      {"reconnect-ms", required_argument, nullptr, 'c'},
      {"read-timeout-ms", required_argument, nullptr, 't'},
      {"duration-sec", required_argument, nullptr, 'u'},
      {"startup-connect-timeout-sec", required_argument, nullptr, 'x'},
      {"reconnect-failure-timeout-sec", required_argument, nullptr, 'f'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0},
  };

  optind = 1;
  opterr = 0;
  while (true) {
    const int c = ::getopt_long(argc, argv, ":o:d:r:e:m:c:t:u:x:f:h", kLongOpts, nullptr);
    if (c == -1) {
      break;
    }
    switch (c) {
      case 'o':
        cfg.output_dir = optarg;
        break;
      case 'd': {
        const std::string device_arg = optarg;
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
        if (!frontend->ParseDeviceSpec(frontend_spec, spec, parse_error)) {
          std::cerr << "Invalid --device for frontend '" << frontend_id << "': " << parse_error << "\n";
          return false;
        }
        cfg.devices.push_back(spec);
        break;
      }
      case 'r': {
        uint32_t tmp = 0;
        if (!parse_u32(optarg, tmp)) {
          std::cerr << "Invalid --Run-start: " << optarg << " (expected >=0)\n";
          return false;
        }
        cfg.run_start = tmp;
        cfg.run_start_specified = true;
        break;
      }
      case 'e': {
        uint32_t tmp = 0;
        if (!parse_u32(optarg, tmp) || tmp == 0) {
          std::cerr << "Invalid --events-per-file: " << optarg << " (expected >=1)\n";
          return false;
        }
        cfg.events_per_file = tmp;
        break;
      }
      case 'm':
        cfg.comment = optarg;
        break;
      case 'c': {
        uint32_t tmp = 0;
        if (!parse_u32(optarg, tmp) || tmp == 0) {
          std::cerr << "Invalid --reconnect-ms: " << optarg << " (expected >=1)\n";
          return false;
        }
        cfg.reconnect_ms = tmp;
        break;
      }
      case 't': {
        uint32_t tmp = 0;
        if (!parse_u32(optarg, tmp) || tmp == 0) {
          std::cerr << "Invalid --read-timeout-ms: " << optarg << " (expected >=1)\n";
          return false;
        }
        cfg.read_timeout_ms = tmp;
        break;
      }
      case 'u': {
        uint32_t tmp = 0;
        if (!parse_u32(optarg, tmp) || tmp == 0) {
          std::cerr << "Invalid --duration-sec: " << optarg << " (expected >=1)\n";
          return false;
        }
        cfg.duration_sec = tmp;
        break;
      }
      case 'x': {
        uint32_t tmp = 0;
        if (!parse_u32(optarg, tmp) || tmp == 0) {
          std::cerr << "Invalid --startup-connect-timeout-sec: " << optarg << " (expected >=1)\n";
          return false;
        }
        cfg.startup_connect_timeout_sec = tmp;
        break;
      }
      case 'f': {
        uint32_t tmp = 0;
        if (!parse_u32(optarg, tmp) || tmp == 0) {
          std::cerr << "Invalid --reconnect-failure-timeout-sec: " << optarg << " (expected >=1)\n";
          return false;
        }
        cfg.reconnect_failure_timeout_sec = tmp;
        break;
      }
      case 'h':
        PrintDaqUsage(argv[0]);
        std::exit(0);
      case ':':
        std::cerr << "Missing value for option: " << argv[optind - 1] << "\n";
        return false;
      default:
        std::cerr << "Unknown argument: " << argv[optind - 1] << "\n";
        return false;
    }
  }

  if (optind < argc) {
    std::cerr << "Unknown argument: " << argv[optind] << "\n";
    return false;
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
