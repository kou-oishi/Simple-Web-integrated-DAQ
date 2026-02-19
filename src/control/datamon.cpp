#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core/defaults.hpp"
#include "monitor/decoder.hpp"
#include "monitor/decoder_registry.hpp"
#include "monitor/frame_source.hpp"
#include "monitor/pipeline.hpp"
#include "monitor/sink.hpp"
#include "monitor/zmq_frame_source.hpp"
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
#include "monitor/sink_root.hpp"
#endif

namespace {

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
  std::cerr << "  --decoder <name[=spec]>   Decoder module and optional module-specific spec\n";
  std::cerr << "                            Example: kc705_tof\n";
  std::cerr << "  --max-events <n>          Maximum events to decode (0 means all)\n";
  std::cerr << "  --print-every <n>         Print every n events (default: 1000)\n";
  std::cerr << "  --run-start <n>           Live mode start run number (default: " << daq_defaults::kRunStart << ")\n";
  std::cerr << "  --poll-ms <n>             Live mode polling interval in ms (default: 200)\n";
  std::cerr << "  --idle-timeout-sec <n>    Live mode stop if no data for n sec (0 means never stop)\n";
  std::cerr << "  --data-endpoint <ep>      Direct data subscribe endpoint (e.g. "
            << daq_defaults::kDataEndpoint << ")\n";
  std::cerr << "  --text-stream             Stream every decoded event as plain text\n";
  std::cerr << "  --text-output <path|- >   Text stream output destination ('-' or omitted means stdout)\n";
  std::cerr << "  --no-console              Disable console sink\n";
  std::cerr << "  --root-out <file.root>    Optional ROOT output (TTree 'events')\n";
  std::cerr << "  --help                    Show this help\n";
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
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--live-output-dir") {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for --live-output-dir\n";
        return false;
      }
      options.live_output_dir = argv[++i];
    } else if (arg == "--data-endpoint") {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for --data-endpoint\n";
        return false;
      }
      options.data_endpoint = argv[++i];
    } else if (arg == "--decoder") {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for --decoder\n";
        return false;
      }
      options.decoder = argv[++i];
    } else if (arg == "--max-events") {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for --max-events\n";
        return false;
      }
      if (!parse_uint64(argv[++i], options.max_events)) {
        std::cerr << "Invalid --max-events\n";
        return false;
      }
    } else if (arg == "--print-every") {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for --print-every\n";
        return false;
      }
      if (!parse_uint64(argv[++i], options.print_every) || options.print_every == 0) {
        std::cerr << "Invalid --print-every. Expected integer > 0\n";
        return false;
      }
    } else if (arg == "--run-start") {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for --run-start\n";
        return false;
      }
      if (!parse_uint32(argv[++i], options.run_start)) {
        std::cerr << "Invalid --run-start\n";
        return false;
      }
    } else if (arg == "--poll-ms") {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for --poll-ms\n";
        return false;
      }
      if (!parse_uint32(argv[++i], options.poll_ms) || options.poll_ms == 0) {
        std::cerr << "Invalid --poll-ms. Expected integer > 0\n";
        return false;
      }
    } else if (arg == "--idle-timeout-sec") {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for --idle-timeout-sec\n";
        return false;
      }
      if (!parse_uint32(argv[++i], options.idle_timeout_sec)) {
        std::cerr << "Invalid --idle-timeout-sec\n";
        return false;
      }
    } else if (arg == "--text-stream") {
      options.text_stream = true;
    } else if (arg == "--text-output") {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for --text-output\n";
        return false;
      }
      options.text_output = argv[++i];
    } else if (arg == "--no-console") {
      options.no_console = true;
    } else if (arg == "--root-out") {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for --root-out\n";
        return false;
      }
      options.root_out = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    } else if (!arg.empty() && arg[0] != '-') {
      if (!options.input_file.empty()) {
        std::cerr << "Multiple input files provided. Use only one positional input file.\n";
        return false;
      }
      options.input_file = arg;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      return false;
    }
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
  if (!pipeline.run(options.max_events, error_text)) {
    std::cerr << "datamon failed: " << error_text << "\n";
    return 1;
  }

  return 0;
}
