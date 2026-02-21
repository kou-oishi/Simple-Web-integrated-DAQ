#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "concrete_modules/module_registry.hpp"
#include "core/defaults.hpp"
#include "monitor/decoder.hpp"
#include "monitor/frame_source.hpp"
#include "monitor/pipeline.hpp"
#include "monitor/sink.hpp"
#include "monitor/sink_realtime.hpp"
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
  std::vector<std::string> analyses;
  bool no_gui = false;
  std::string snapshot_dir;
  uint32_t snapshot_interval_ms = 1000;
  std::string snapshot_select_endpoint;
  bool list_modules = false;
  bool json_output = false;
};

std::string json_escape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (const char ch : text) {
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

void print_modules_json() {
  const auto decoders = ListMonitorDecoderFactories();
  const auto analyses = ListRealtimeAnalysisFactories();

  std::cout << "{\"decoders\":[";
  for (std::size_t i = 0; i < decoders.size(); ++i) {
    if (i > 0) {
      std::cout << ",";
    }
    std::string title = decoders[i];
    if (const auto* factory = FindMonitorDecoderFactory(decoders[i])) {
      if (factory->Title() != nullptr) {
        title = factory->Title();
      }
    }
    std::cout << "{\"name\":\"" << json_escape(decoders[i]) << "\",\"title\":\"" << json_escape(title) << "\"}";
  }
  std::cout << "],\"analyses\":[";
  for (std::size_t i = 0; i < analyses.size(); ++i) {
    if (i > 0) {
      std::cout << ",";
    }
    std::string expected_decoder;
    if (const auto* factory = FindRealtimeAnalysisFactory(analyses[i])) {
      if (factory->ExpectedDecoder() != nullptr) {
        expected_decoder = factory->ExpectedDecoder();
      }
      std::string title = analyses[i];
      if (factory->Title() != nullptr) {
        title = factory->Title();
      }
      std::cout << "{\"name\":\"" << json_escape(analyses[i]) << "\",\"title\":\"" << json_escape(title)
                << "\",\"expected_decoder\":\"" << json_escape(expected_decoder) << "\"}";
      continue;
    }
    std::cout << "{\"name\":\"" << json_escape(analyses[i]) << "\",\"title\":\"" << json_escape(analyses[i])
              << "\",\"expected_decoder\":\"" << json_escape(expected_decoder) << "\"}";
  }
  std::cout << "]}\n";
}

void print_usage(const char* prog) {
  std::cerr << "Usage: " << prog
            << " [input_file.dat] [--live-output-dir <dir> | --data-endpoint <zmq-endpoint>] [options]\n";
  std::cerr << "Options:\n";
  std::cerr << "  -l, --live-output-dir <dir>  Read live Run files from output directory\n";
  std::cerr << "  -e, --data-endpoint <ep>      Subscribe directly to ZMQ data endpoint (default: "
            << daq_defaults::kDataEndpoint << ")\n";
  std::cerr << "  -d, --decoder <Name[=spec]>   Decoder module and optional decoder spec (e.g. kc705_tof)\n";
  std::cerr << "  -m, --max-events <n>          Maximum events to decode (0 means all)\n";
  std::cerr << "  -p, --print-every <n>         Console summary interval (default: 1000)\n";
  std::cerr << "  -r, --Run-start <n>           Live mode starting Run number (default: "
            << daq_defaults::kRunStart << ")\n";
  std::cerr << "  -q, --poll-ms <n>             Poll interval in milliseconds (default: 200)\n";
  std::cerr << "  -i, --idle-timeout-sec <n>    Stop after n seconds with no data (0 means never)\n";
  std::cerr << "  -t, --text-stream             Enable per-Event text output\n";
  std::cerr << "  -o, --text-output <path|->    Text output destination ('-' means stdout)\n";
  std::cerr << "  -n, --no-console              Disable default console sink\n";
  std::cerr << "  -O, --root-out <file.root>    Write decoded events to ROOT TTree output\n";
  std::cerr << "  -a, --analysis <Name[=spec]>  Enable realtime analysis module (repeatable)\n";
  std::cerr << "      --no-gui                  Disable ROOT GUI event loop (batch mode)\n";
  std::cerr << "      --snapshot-dir <dir>      Save analysis canvas snapshots as PNG files\n";
  std::cerr << "      --snapshot-interval-ms <n> Snapshot interval in milliseconds (default: 1000)\n";
  std::cerr << "      --snapshot-select-endpoint <ep> ZMQ REP endpoint for selected analysis control\n";
  std::cerr << "      --list-modules            Print available decoders and analyses, then exit\n";
  std::cerr << "      --json                    Use JSON output with --list-modules\n";
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
      {"Run-start", required_argument, nullptr, 'r'},
      {"poll-ms", required_argument, nullptr, 'q'},
      {"idle-timeout-sec", required_argument, nullptr, 'i'},
      {"text-stream", no_argument, nullptr, 't'},
      {"text-output", required_argument, nullptr, 'o'},
      {"no-console", no_argument, nullptr, 'n'},
      {"root-out", required_argument, nullptr, 'O'},
      {"analysis", required_argument, nullptr, 'a'},
      {"no-gui", no_argument, nullptr, 1002},
      {"snapshot-dir", required_argument, nullptr, 1003},
      {"snapshot-interval-ms", required_argument, nullptr, 1004},
      {"snapshot-select-endpoint", required_argument, nullptr, 1005},
      {"list-modules", no_argument, nullptr, 1000},
      {"json", no_argument, nullptr, 1001},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0},
  };

  optind = 1;
  opterr = 0;
  while (true) {
    const int c = ::getopt_long(argc, argv, ":l:e:d:m:p:r:q:i:to:nO:a:h", kLongOpts, nullptr);
    if (c == -1) {
      break;
    }
    switch (c) {
      case 1000:
        options.list_modules = true;
        break;
      case 1001:
        options.json_output = true;
        break;
      case 1002:
        options.no_gui = true;
        break;
      case 1003:
        options.snapshot_dir = optarg;
        break;
      case 1004:
        if (!parse_uint32(optarg, options.snapshot_interval_ms) || options.snapshot_interval_ms == 0) {
          std::cerr << "Invalid --snapshot-interval-ms. Expected integer > 0\n";
          return false;
        }
        break;
      case 1005:
        options.snapshot_select_endpoint = optarg;
        break;
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
          std::cerr << "Invalid --Run-start\n";
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
      case 'a':
        options.analyses.emplace_back(optarg);
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

  if (options.list_modules) {
    return true;
  }

  if (!options.input_file.empty() && options.root_out.empty() && options.no_console && !options.text_stream &&
      options.analyses.empty()) {
    std::cerr << "When --no-console is set, specify --root-out, --text-stream, and/or --analysis\n";
    return false;
  }

  if (!options.text_output.empty() && !options.text_stream) {
    std::cerr << "--text-output requires --text-stream\n";
    return false;
  }

  return true;
}

void split_decoder_arg(const std::string& arg, std::string& Name, std::string& spec) {
  const std::size_t eq = arg.find('=');
  if (eq == std::string::npos) {
    Name = arg;
    spec.clear();
    return;
  }
  Name = arg.substr(0, eq);
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

  if (options.list_modules) {
    if (options.json_output) {
      print_modules_json();
    } else {
      std::cout << "Decoders:";
      for (const auto& Name : ListMonitorDecoderFactories()) {
        std::cout << " " << Name;
      }
      std::cout << "\nAnalyses:";
      for (const auto& Name : ListRealtimeAnalysisFactories()) {
        std::cout << " " << Name;
      }
      std::cout << "\n";
    }
    return 0;
  }

  std::string decoder_name;
  std::string decoder_spec;
  split_decoder_arg(options.decoder, decoder_name, decoder_spec);
  if (decoder_name.empty()) {
    std::cerr << "Decoder Name is empty\n";
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
  if (!factory->Create(decoder_spec, decoder, frame_size, error_text)) {
    std::cerr << "Failed to Create decoder '" << decoder_name << "': " << error_text << "\n";
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

  if (!options.analyses.empty()) {
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
    std::vector<std::unique_ptr<IRealtimeAnalysis>> analyses;
    std::vector<std::string> analysis_names;
    analyses.reserve(options.analyses.size());
    analysis_names.reserve(options.analyses.size());
    for (const auto& analysis_arg : options.analyses) {
      std::string analysis_name;
      std::string analysis_spec;
      split_decoder_arg(analysis_arg, analysis_name, analysis_spec);
      if (analysis_name.empty()) {
        std::cerr << "Analysis Name is empty\n";
        return 1;
      }

      const IMonitorRealtimeAnalysisFactory* factory = FindRealtimeAnalysisFactory(analysis_name);
      if (factory == nullptr) {
        std::cerr << "Unsupported analysis: " << analysis_name << "\n";
        std::cerr << "Available analyses:";
        const auto available = ListRealtimeAnalysisFactories();
        if (available.empty()) {
          std::cerr << " (none)";
        } else {
          for (const auto& n : available) {
            std::cerr << " " << n;
          }
        }
        std::cerr << "\n";
        return 1;
      }

      const std::string ExpectedDecoder = factory->ExpectedDecoder() == nullptr ? "" : factory->ExpectedDecoder();
      if (!ExpectedDecoder.empty() && ExpectedDecoder != decoder_name) {
        std::cerr << "Analysis '" << analysis_name << "' requires decoder '" << ExpectedDecoder
                  << "', but selected decoder is '" << decoder_name << "'\n";
        return 1;
      }

      ParsedAnalysisSpec parsed_spec;
      if (!parsed_spec.Parse(analysis_spec, error_text)) {
        std::cerr << "Invalid spec for analysis '" << analysis_name << "': " << error_text << "\n";
        return 1;
      }

      std::unique_ptr<IRealtimeAnalysis> analysis;
      if (!factory->Create(parsed_spec, analysis, error_text)) {
        std::cerr << "Failed to Create analysis '" << analysis_name << "': " << error_text << "\n";
        return 1;
      }
      if (!analysis) {
        std::cerr << "Analysis factory returned null analysis: " << analysis_name << "\n";
        return 1;
      }
      analyses.push_back(std::move(analysis));
      analysis_names.push_back(analysis_name);
    }
    RealtimeAnalysisSink::Options sink_options;
    sink_options.enable_gui = !options.no_gui;
    sink_options.snapshot_dir = options.snapshot_dir;
    sink_options.snapshot_interval_ms = options.snapshot_interval_ms;
    sink_options.snapshot_select_endpoint = options.snapshot_select_endpoint;
    sinks.push_back(
        std::make_unique<RealtimeAnalysisSink>(std::move(analyses), std::move(analysis_names), std::move(sink_options)));
#else
    std::cerr << "This build does not support realtime analysis. Rebuild with ROOT installed.\n";
    return 1;
#endif
  }

  if (sinks.empty()) {
    std::cerr << "No output sink selected. Enable console output, --root-out, --text-stream, or --analysis.\n";
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
  if (!pipeline.Run(options.max_events, error_text, &g_stop_requested)) {
    std::cerr << "datamon failed: " << error_text << "\n";
    return 1;
  }

  return 0;
}
