#include <cctype>
#include <getopt.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "control_api/zmq_control_client.hpp"
#include "core/daq_cli.hpp"
#include "core/defaults.hpp"

namespace {

void print_usage(const char* prog) {
  std::cerr << "Usage: " << prog << " [--endpoint <zmq-endpoint>] <command> [args...]\n";
  std::cerr << "Options:\n";
  std::cerr << "  -e, --endpoint <ep>  Control endpoint to Connect\n";
  std::cerr << "  -h, --help           Show this help\n";
  std::cerr << "Commands:\n";
  std::cerr << "  status\n";
  std::cerr << "  start <daq args>\n";
  std::cerr << "  stop\n";
  std::cerr << "  shutdown\n";
  std::cerr << "Default control endpoint: " << daq_defaults::kControlEndpoint << "\n";
}

std::string join_args(const std::vector<std::string>& args, size_t from) {
  std::ostringstream oss;
  for (size_t i = from; i < args.size(); ++i) {
    if (i > from) {
      oss << ' ';
    }
    oss << args[i];
  }
  return oss.str();
}

std::string to_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  std::string endpoint = daq_defaults::kControlEndpoint;

  static constexpr option kLongOpts[] = {
      {"endpoint", required_argument, nullptr, 'e'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0},
  };

  optind = 1;
  opterr = 0;
  while (true) {
    const int c = ::getopt_long(argc, argv, "+:e:h", kLongOpts, nullptr);
    if (c == -1) {
      break;
    }
    switch (c) {
      case 'e':
        endpoint = optarg;
        break;
      case 'h':
        print_usage(argv[0]);
        return 0;
      case ':':
        std::cerr << "Missing value for option: " << argv[optind - 1] << "\n";
        print_usage(argv[0]);
        return 1;
      default:
        std::cerr << "Unknown argument: " << argv[optind - 1] << "\n";
        print_usage(argv[0]);
        return 1;
    }
  }

  if (optind >= argc) {
    print_usage(argv[0]);
    return 1;
  }

  const std::string cmd = to_lower(argv[optind]);

  std::string req;
  if (cmd == "status" || cmd == "stop" || cmd == "shutdown") {
    req = cmd;
  } else if (cmd == "start") {
    if (optind + 1 < argc) {
      const std::string first_start_arg = argv[optind + 1];
      if (first_start_arg == "-h" || first_start_arg == "--help") {
        PrintDaqUsage("daqctl start");
        return 0;
      }
    }

    req = "start";
    if (optind + 1 < argc) {
      req += " ";
      std::vector<std::string> args;
      args.reserve(static_cast<size_t>(argc - optind - 1));
      for (int i = optind + 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
      }
      req += join_args(args, 0);
    }
  } else {
    std::cerr << "Unknown command: " << cmd << "\n";
    print_usage(argv[0]);
    return 1;
  }

  ZmqControlClient client(endpoint);
  std::string reply;
  std::string error_text;
  if (!client.Request(req, reply, error_text)) {
    std::cerr << error_text << "\n";
    return 1;
  }

  std::cout << reply << "\n";
  return (reply.rfind("error", 0) == 0) ? 1 : 0;
}
