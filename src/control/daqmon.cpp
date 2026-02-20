#include <getopt.h>
#include <iostream>
#include <string>

#include "control_api/zmq_status_subscriber.hpp"
#include "core/defaults.hpp"

namespace {

void print_usage(const char* prog) {
  std::cerr << "Usage: " << prog << " [--status-endpoint <zmq-endpoint>]\n";
  std::cerr << "Options:\n";
  std::cerr << "  -s, --status-endpoint <ep>  Status endpoint to subscribe\n";
  std::cerr << "  -h, --help                  Show this help\n";
  std::cerr << "Default status endpoint: " << daq_defaults::kStatusEndpoint << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string status_endpoint = daq_defaults::kStatusEndpoint;

  static constexpr option kLongOpts[] = {
      {"status-endpoint", required_argument, nullptr, 's'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0},
  };

  optind = 1;
  opterr = 0;
  while (true) {
    const int c = ::getopt_long(argc, argv, ":s:h", kLongOpts, nullptr);
    if (c == -1) {
      break;
    }
    switch (c) {
      case 's':
        status_endpoint = optarg;
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
  if (optind < argc) {
    std::cerr << "Unknown argument: " << argv[optind] << "\n";
    print_usage(argv[0]);
    return 1;
  }

  ZmqStatusSubscriber sub(status_endpoint);
  std::string error_text;
  if (!sub.connect(error_text)) {
    std::cerr << error_text << "\n";
    return 1;
  }

  while (true) {
    std::string payload;
    if (!sub.receive_next(payload, error_text)) {
      std::cerr << error_text << "\n";
      return 1;
    }
    std::cout << payload << "\n";
  }
}
