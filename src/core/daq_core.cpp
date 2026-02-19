#include <csignal>

#include "core/daq_cli.hpp"
#include "core/daq_runtime.hpp"

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void handle_signal(int /*signum*/) { g_stop_requested = 1; }

}  // namespace

int main(int argc, char** argv) {
  DaqConfig cfg;
  if (!ParseDaqArgs(argc, argv, cfg)) {
    PrintDaqUsage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  return RunDaqCore(cfg, g_stop_requested);
}
