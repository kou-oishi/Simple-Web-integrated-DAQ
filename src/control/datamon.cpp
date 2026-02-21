#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core/defaults.hpp"
#include "monitor/decoder.hpp"
#include "concrete_modules/module_registry.hpp"
#include "monitor/frame_source.hpp"
#include "monitor/pipeline.hpp"
#include "monitor/sink.hpp"
#include "monitor/zmq_frame_source.hpp"
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
#include "monitor/sink_root.hpp"
#endif

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void handle_signal(int /*signum*/) { g_stop_requested = 1; }

struct Options {
  std::string input_file;
  std::string live_output_dir;
  std::string data_endpoint;
  std::string decoder = "kc705_tof";
  uint64_t max_events = 0;
  uint64_t print_every = 1000;
  uint32_t run_start = daq_defaults::kRunStart;
  uint32_t poll_ms = 200;
  uint32_t idle_timeout_sec = 0;
  bool text_stream = false;
  std::string text_output;
  bool no_console = false;
  std::string root_out;
};

void print_usage(const char* prog) {
  std::cerr << "Usage: " << prog
            << " [input_file.dat] [--live-output-dir <dir> | --data-endpoint <zmq-endpoint>] [options]\n";
  std::cerr << "Options:\n";
  std::cerr << "  -l, --live-output-dir <dir>  Read live run files from output directory\n";
  std::cerr << "  -e, --data-endpoint <ep>      Subscribe directly to ZMQ data endpoint (default: "
            << daq_defaults::kDataEndpoint << ")\n";
  std::cerr << "  -d, --decoder <name[=spec]>   Decoder module and optional decoder spec (e.g. kc705_tof)\n";
  std::cerr << "  -m, --max-events <n>          Maximum events to decode (0 means all)\n";
  std::cerr << "  -p, --print-every <n>         Console summary interval (default: 1000)\n";
  std::cerr << "  -r, --run-start <n>           Live mode starting run number (default: "
            << daq_defaults::kRunStart << ")\n";
  std::cerr << "  -q, --poll-ms <n>             Poll interval in milliseconds (default: 200)\n";
  std::cerr << "  -i, --idle-timeout-sec <n>    Stop after n seconds with no data (0 means never)\n";
  std::cerr << "  -t, --text-stream             Enable per-event text output\n";
  std::cerr << "  -o, --text-output <path|->    Text output destination ('-' means stdout)\n";
  std::cerr << "  -n, --no-console              Disable default console sink\n";
  std::cerr << "  -O, --root-out <file.root>    Write decoded events to ROOT TTree output\n";
  std::cerr << "  -h, --help                    Show this help\n";
}

bool parse_uint64(const std::string& text, uint64_t& out) {
  try {
    std::size_t pos = 0;
    const unsigned long long value = std::stoull(text, &pos, 10);
    if (pos != text.size()) {
      return false;
    }
    out = static_cast<uint64_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_uint32(const std::string& text, uint32_t& out) {
  uint64_t value = 0;
  if (!parse_uint64(text, value) || value > 0xFFFFFFFFULL) {
    return false;
  }
  out = static_cast<uint32_t>(value);
  return true;
}

bool parse_args(int argc, char** argv, Options& options) {
  static constexpr option kLongOpts[] = {
      {"live-output-dir", required_argument, nullptr, 'l'},
      {"data-endpoint", required_argument, nullptr, 'e'},
      {"decoder", required_argument, nullptr, 'd'},
      {"max-events", required_argument, nullptr, 'm'},
      {"print-every", required_argument, nullptr, 'p'},
      {"run-start", required_argument, nullptr, 'r'},
      {"poll-ms", required_argument, nullptr, 'q'},
      {"idle-timeout-sec", required_argument, nullptr, 'i'},
      {"text-stream", no_argument, nullptr, 't'},
      {"text-output", required_argument, nullptr, 'o'},
      {"no-console", no_argument, nullptr, 'n'},
      {"root-out", required_argument, nullptr, 'O'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0},
  };

  optind = 1;
  opterr = 0;
  while (true) {
    const int c = ::getopt_long(argc, argv, ":l:e:d:m:p:r:q:i:to:nO:h", kLongOpts, nullptr);
    if (c == -1) {
      break;
    }
    switch (c) {
      case 'l':
        options.live_output_dir = optarg;
        break;
      case 'e':
        options.data_endpoint = optarg;
        break;
      case 'd':
        options.decoder = optarg;
        break;
      case 'm':
        if (!parse_uint64(optarg, options.max_events)) {
          std::cerr << "Invalid --max-events\n";
          return false;
        }
        break;
      case 'p':
        if (!parse_uint64(optarg, options.print_every) || options.print_every == 0) {
          std::cerr << "Invalid --print-every. Expected integer > 0\n";
          return false;
        }
        break;
      case 'r':
        if (!parse_uint32(optarg, options.run_start)) {
          std::cerr << "Invalid --run-start\n";
          return false;
        }
        break;
      case 'q':
        if (!parse_uint32(optarg, options.poll_ms) || options.poll_ms == 0) {
          std::cerr << "Invalid --poll-ms. Expected integer > 0\n";
          return false;
        }
        break;
      case 'i':
        if (!parse_uint32(optarg, options.idle_timeout_sec)) {
          std::cerr << "Invalid --idle-timeout-sec\n";
          return false;
        }
        break;
      case 't':
        options.text_stream = true;
        break;
      case 'o':
        options.text_output = optarg;
        break;
      case 'n':
        options.no_console = true;
        break;
      case 'O':
        options.root_out = optarg;
        break;
      case 'h':
        print_usage(argv[0]);
        std::exit(0);
      case ':':
        std::cerr << "Missing value for option: " << argv[optind - 1] << "\n";
        return false;
      default:
        std::cerr << "Unknown argument: " << argv[optind - 1] << "\n";
        return false;
    }
  }

  for (int i = optind; i < argc; ++i) {
    const std::string arg = argv[i];
    if (!arg.empty() && arg[0] != '-') {
      if (!options.input_file.empty()) {
        std::cerr << "Multiple input files provided. Use only one positional input file.\n";
        return false;
      }
      options.input_file = arg;
      continue;
    }
    std::cerr << "Unknown argument: " << arg << "\n";
    return false;
  }

  int source_count = 0;
  source_count += options.input_file.empty() ? 0 : 1;
  source_count += options.live_output_dir.empty() ? 0 : 1;
  source_count += options.data_endpoint.empty() ? 0 : 1;
  if (source_count > 1) {
    std::cerr << "Specify at most one source: positional input file, --live-output-dir, or --data-endpoint\n";
    return false;
  }
  if (source_count == 0) {
    options.data_endpoint = daq_defaults::kDataEndpoint;
  }

  if (!options.input_file.empty() && options.root_out.empty() && options.no_console) {
    if (!options.text_stream) {
      std::cerr << "When --no-console is set, specify --root-out and/or --text-stream\n";
      return false;
    }
  }

  if (!options.text_output.empty() && !options.text_stream) {
    std::cerr << "--text-output requires --text-stream\n";
    return false;
  }

  return true;
}

void split_decoder_arg(const std::string& arg, std::string& name, std::string& spec) {
  const std::size_t eq = arg.find('=');
  if (eq == std::string::npos) {
    name = arg;
    spec.clear();
    return;
  }
  name = arg.substr(0, eq);
  spec = arg.substr(eq + 1);
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  Options options;
  if (!parse_args(argc, argv, options)) {
    print_usage(argv[0]);
    return 1;
  }

  std::string decoder_name;
  std::string decoder_spec;
  split_decoder_arg(options.decoder, decoder_name, decoder_spec);
  if (decoder_name.empty()) {
    std::cerr << "Decoder name is empty\n";
    return 1;
  }

  const IMonitorDecoderFactory* factory = FindMonitorDecoderFactory(decoder_name);
  if (factory == nullptr) {
    std::cerr << "Unsupported decoder: " << decoder_name << "\n";
    std::cerr << "Available decoders:";
    for (const auto& n : ListMonitorDecoderFactories()) {
      std::cerr << " " << n;
    }
    std::cerr << "\n";
    return 1;
  }

  std::size_t frame_size = 0;
  std::string error_text;
  std::unique_ptr<IDecoder> decoder;
  if (!factory->create(decoder_spec, decoder, frame_size, error_text)) {
    std::cerr << "Failed to create decoder '" << decoder_name << "': " << error_text << "\n";
    return 1;
  }
  if (decoder == nullptr || frame_size == 0) {
    std::cerr << "Decoder factory returned invalid decoder/frame size\n";
    return 1;
  }

  std::vector<std::unique_ptr<IEventSink>> sinks;
  if (!options.no_console) {
    sinks.push_back(std::make_unique<TextSink>(decoder.get(), "-", options.print_every, true));
  }
  if (options.text_stream) {
    const std::string out = options.text_output.empty() ? "-" : options.text_output;
    sinks.push_back(std::make_unique<TextSink>(decoder.get(), out, 1, false));
  }

  if (!options.root_out.empty()) {
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
    sinks.push_back(std::make_unique<RootTreeSink>(decoder.get(), options.root_out));
#else
    std::cerr << "This build does not support ROOT output. Rebuild with ROOT installed.\n";
    return 1;
#endif
  }

  if (sinks.empty()) {
    std::cerr << "No output sink selected. Enable console output or set --root-out.\n";
    return 1;
  }

  std::unique_ptr<IFrameSource> source;
  if (!options.input_file.empty()) {
    source = std::make_unique<FileFrameSource>(options.input_file, frame_size);
  } else if (!options.live_output_dir.empty()) {
    source = std::make_unique<LiveRunFileSource>(options.live_output_dir, frame_size, options.run_start, options.poll_ms,
                                                 options.idle_timeout_sec);
  } else {
    source = std::make_unique<ZmqDataFrameSource>(options.data_endpoint, options.poll_ms, options.idle_timeout_sec);
  }
  MonitorPipeline pipeline(std::move(source), std::move(decoder), std::move(sinks));

  error_text.clear();
  if (!pipeline.run(options.max_events, error_text, &g_stop_requested)) {
    std::cerr << "datamon failed: " << error_text << "\n";
    return 1;
  }

  return 0;
}
