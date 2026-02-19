#include <iostream>
#include <string>

#include "control_api/zmq_status_subscriber.hpp"
#include "core/defaults.hpp"

namespace {

void print_usage(const char* prog) {
  std::cerr << "Usage: " << prog << " [--status-endpoint <zmq-endpoint>]\n";
  std::cerr << "Default status endpoint: " << daq_defaults::kStatusEndpoint << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string status_endpoint = daq_defaults::kStatusEndpoint;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--status-endpoint") {
      if (i + 1 >= argc) {
        print_usage(argv[0]);
        return 1;
      }
      status_endpoint = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      print_usage(argv[0]);
      return 1;
    }
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
