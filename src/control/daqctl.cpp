#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "control_api/zmq_control_client.hpp"
#include "core/defaults.hpp"

namespace {

void print_usage(const char* prog) {
  std::cerr << "Usage: " << prog << " [--endpoint <zmq-endpoint>] <command> [args...]\n";
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

  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }

  size_t pos = 0;
  while (pos < args.size()) {
    if (args[pos] == "--help" || args[pos] == "-h") {
      print_usage(argv[0]);
      return 0;
    }
    if (args[pos] == "--endpoint") {
      if (pos + 1 >= args.size()) {
        print_usage(argv[0]);
        return 1;
      }
      endpoint = args[pos + 1];
      pos += 2;
      continue;
    }
    break;
  }

  if (pos >= args.size()) {
    print_usage(argv[0]);
    return 1;
  }

  const std::string cmd = to_lower(args[pos]);

  std::string req;
  if (cmd == "status" || cmd == "stop" || cmd == "shutdown") {
    req = cmd;
  } else if (cmd == "start") {
    req = "start";
    if (pos + 1 < args.size()) {
      req += " ";
      req += join_args(args, pos + 1);
    }
  } else {
    std::cerr << "Unknown command: " << cmd << "\n";
    print_usage(argv[0]);
    return 1;
  }

  ZmqControlClient client(endpoint);
  std::string reply;
  std::string error_text;
  if (!client.request(req, reply, error_text)) {
    std::cerr << error_text << "\n";
    return 1;
  }

  std::cout << reply << "\n";
  return (reply.rfind("error", 0) == 0) ? 1 : 0;
}
