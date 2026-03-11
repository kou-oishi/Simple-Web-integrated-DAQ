#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
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
  std::vector<std::string> input_files;
  std::string data_endpoint;
  std::string raw_dir;
  std::string dec_dir;
  bool run_selected = false;
  uint32_t run_start_number = 0;
  uint32_t run_end_number = 0;
  bool subrun_selected = false;
  uint32_t subrun_start_number = 0;
  uint32_t subrun_end_number = 0;
  std::string decoder = "kc705_tof";
  uint64_t max_events = 0;
  uint64_t print_every = 1000;
  uint32_t poll_ms = 200;
  uint32_t idle_timeout_sec = 0;
  bool text_stream = false;
  std::string text_output;
  bool no_console = false;
  bool root_out_requested = false;
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
  std::cerr << "Usage:\n";
  std::cerr << "  " << prog << " [input_file.dat ...] [options]\n";
  std::cerr << "  " << prog << " --run <n|n-m> [--subrun <n|n-m>] [--raw-dir <dir>] [options]\n";
  std::cerr << "  " << prog << " --data-endpoint <ep> [options]\n";
  std::cerr << "\n";
  std::cerr << "Input source selection (mutually exclusive):\n";
  std::cerr << "  1) positional input file(s)\n";
  std::cerr << "  2) --run/--subrun selection from raw directory\n";
  std::cerr << "  3) --data-endpoint\n";
  std::cerr << "  If none is specified, --data-endpoint defaults to " << daq_defaults::kDataEndpoint << "\n";
  std::cerr << "\n";
  std::cerr << "Options:\n";
  std::cerr << "      --run <n|n-m>             Select run number or inclusive run range from raw dir\n";
  std::cerr << "      --subrun <n|n-m>          With --run, select subrun number/range\n";
  std::cerr << "                                If omitted, scans subrun 0,1,2,... until a missing file\n";
  std::cerr << "      --raw-dir <dir>           Raw input directory for --run mode (default: $RAWDIR)\n";
  std::cerr << "      --dec-dir <dir>           Output directory for per-input ROOT output (default: $DECDIR)\n";
  std::cerr << "  -e, --data-endpoint <ep>      Subscribe directly to ZMQ data endpoint\n";
  std::cerr << "  -d, --decoder <Name[=spec]>   Decoder module and optional decoder spec (e.g. kc705_tof)\n";
  std::cerr << "  -m, --max-events <n>          Maximum events to decode (0 means all)\n";
  std::cerr << "  -p, --print-every <n>         Text stream interval for -t (default: 1000)\n";
  std::cerr << "  -r, --run <n|n-m>             Select run number/range from raw dir mode\n";
  std::cerr << "  -s, --subrun <n|n-m>          Select subrun number/range (requires --run)\n";
  std::cerr << "  -q, --poll-ms <n>             Poll interval in milliseconds (default: 200)\n";
  std::cerr << "  -i, --idle-timeout-sec <n>    Stop after n seconds with no data (0 means never)\n";
  std::cerr << "  -t, --text-stream             Enable per-Event text output\n";
  std::cerr << "  -o, --text-output <path|->    Text output destination ('-' means stdout)\n";
  std::cerr << "  -n, --no-console              Silence all text output from datamon (including -t)\n";
  std::cerr << "  -O, --root-out[=<file.root>]  Enable ROOT output\n";
  std::cerr << "                                With value: write all decoded events to one ROOT file\n";
  std::cerr << "                                Without value: write per-input same-name .root under --dec-dir/$DECDIR\n";
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

bool parse_u32_range(const std::string& text, uint32_t& out_begin, uint32_t& out_end) {
  const std::size_t dash = text.find('-');
  if (dash == std::string::npos) {
    if (!parse_uint32(text, out_begin)) {
      return false;
    }
    out_end = out_begin;
    return true;
  }
  if (dash == 0 || dash + 1 >= text.size()) {
    return false;
  }
  const std::string left = text.substr(0, dash);
  const std::string right = text.substr(dash + 1);
  uint32_t begin = 0;
  uint32_t end = 0;
  if (!parse_uint32(left, begin) || !parse_uint32(right, end)) {
    return false;
  }
  if (begin > end) {
    return false;
  }
  out_begin = begin;
  out_end = end;
  return true;
}

std::string make_run_subrun_path(const std::string& dir, uint32_t run_number, uint32_t subrun_number) {
  std::ostringstream oss;
  oss << "run" << std::setw(daq_defaults::kRunNumberWidth) << std::setfill('0') << run_number
      << "_sub" << std::setw(daq_defaults::kRunNumberWidth) << std::setfill('0') << subrun_number << ".dat";
  return (std::filesystem::path(dir) / oss.str()).string();
}

std::string make_per_input_root_path(const std::string& dec_dir, const std::string& input_file_path) {
  const std::filesystem::path in_path(input_file_path);
  const std::string stem = in_path.stem().string();
  return (std::filesystem::path(dec_dir) / (stem + ".root")).string();
}

bool append_run_selected_files(const std::string& raw_dir,
                               uint32_t run_begin,
                               uint32_t run_end,
                               bool subrun_selected,
                               uint32_t subrun_begin,
                               uint32_t subrun_end,
                               std::vector<std::string>& out_files,
                               std::string& out_error) {
  out_error.clear();
  out_files.clear();
  for (uint32_t run = run_begin; run <= run_end; ++run) {
    std::size_t before_count = out_files.size();
    if (subrun_selected) {
      for (uint32_t sub = subrun_begin; sub <= subrun_end; ++sub) {
        const std::string path = make_run_subrun_path(raw_dir, run, sub);
        if (!std::filesystem::exists(path)) {
          out_error = "input file not found for selected run/subrun: " + path;
          return false;
        }
        out_files.push_back(path);
      }
    } else {
      for (uint32_t sub = 0;; ++sub) {
        const std::string path = make_run_subrun_path(raw_dir, run, sub);
        if (!std::filesystem::exists(path)) {
          break;
        }
        out_files.push_back(path);
      }
    }
    if (out_files.size() == before_count) {
      out_error = "no subrun files found for run " + std::to_string(run) + " in " + raw_dir;
      return false;
    }
    if (run == 0xFFFFFFFFU) {
      break;
    }
  }
  if (out_files.empty()) {
    out_error = "no files found in selected run/subrun range";
    return false;
  }
  return true;
}

bool parse_args(int argc, char** argv, Options& options) {
  static constexpr option kLongOpts[] = {
      {"data-endpoint", required_argument, nullptr, 'e'},
      {"decoder", required_argument, nullptr, 'd'},
      {"max-events", required_argument, nullptr, 'm'},
      {"print-every", required_argument, nullptr, 'p'},
      {"run", required_argument, nullptr, 'r'},
      {"subrun", required_argument, nullptr, 's'},
      {"poll-ms", required_argument, nullptr, 'q'},
      {"idle-timeout-sec", required_argument, nullptr, 'i'},
      {"text-stream", no_argument, nullptr, 't'},
      {"text-output", required_argument, nullptr, 'o'},
      {"no-console", no_argument, nullptr, 'n'},
      {"root-out", optional_argument, nullptr, 'O'},
      {"analysis", required_argument, nullptr, 'a'},
      {"no-gui", no_argument, nullptr, 1002},
      {"snapshot-dir", required_argument, nullptr, 1003},
      {"snapshot-interval-ms", required_argument, nullptr, 1004},
      {"snapshot-select-endpoint", required_argument, nullptr, 1005},
      {"raw-dir", required_argument, nullptr, 1008},
      {"dec-dir", required_argument, nullptr, 1009},
      {"list-modules", no_argument, nullptr, 1000},
      {"json", no_argument, nullptr, 1001},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0},
  };

  optind = 1;
  opterr = 0;
  while (true) {
    const int c = ::getopt_long(argc, argv, ":e:d:m:p:r:s:q:i:to:nO::a:h", kLongOpts, nullptr);
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
      case 'r':
        if (!parse_u32_range(optarg, options.run_start_number, options.run_end_number)) {
          std::cerr << "Invalid --run (expected n or n-m)\n";
          return false;
        }
        options.run_selected = true;
        break;
      case 's':
        if (!parse_u32_range(optarg, options.subrun_start_number, options.subrun_end_number)) {
          std::cerr << "Invalid --subrun (expected n or n-m)\n";
          return false;
        }
        options.subrun_selected = true;
        break;
      case 1008:
        options.raw_dir = optarg;
        break;
      case 1009:
        options.dec_dir = optarg;
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
        options.root_out_requested = true;
        options.root_out = (optarg == nullptr) ? "" : std::string(optarg);
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
      options.input_files.push_back(arg);
      continue;
    }
    std::cerr << "Unknown argument: " << arg << "\n";
    return false;
  }

  if (options.subrun_selected && !options.run_selected) {
    std::cerr << "--subrun requires --run\n";
    return false;
  }

  if (options.run_selected) {
    if (!options.input_files.empty()) {
      std::cerr << "Do not mix positional input files with --run/--subrun selection\n";
      return false;
    }
    if (options.raw_dir.empty()) {
      const char* env_raw = std::getenv("RAWDIR");
      if (env_raw != nullptr) {
        options.raw_dir = env_raw;
      }
    }
    if (options.raw_dir.empty()) {
      std::cerr << "--run requires --raw-dir or RAWDIR environment variable\n";
      return false;
    }

    std::vector<std::string> selected_files;
    std::string select_error;
    if (!append_run_selected_files(options.raw_dir, options.run_start_number, options.run_end_number,
                                   options.subrun_selected, options.subrun_start_number, options.subrun_end_number,
                                   selected_files, select_error)) {
      std::cerr << select_error << "\n";
      return false;
    }
    options.input_files = selected_files;
  }

  if (!options.dec_dir.empty() && !options.root_out_requested) {
    std::cerr << "--dec-dir requires --root-out\n";
    return false;
  }

  if (options.root_out_requested && options.root_out.empty()) {
    if (options.input_files.empty()) {
      std::cerr << "--root-out without file path requires positional input file(s) or --run/--subrun\n";
      return false;
    }
    if (options.dec_dir.empty()) {
      const char* env_dec = std::getenv("DECDIR");
      if (env_dec != nullptr) {
        options.dec_dir = env_dec;
      }
    }
    if (options.dec_dir.empty()) {
      std::cerr << "--root-out without path requires --dec-dir or DECDIR environment variable\n";
      return false;
    }
  }

  int source_count = 0;
  source_count += options.input_files.empty() ? 0 : 1;
  source_count += options.data_endpoint.empty() ? 0 : 1;
  if (source_count > 1) {
    std::cerr << "Specify at most one source: positional input file(s)/--run or --data-endpoint\n";
    return false;
  }
  if (source_count == 0) {
    options.data_endpoint = daq_defaults::kDataEndpoint;
  }

  if (options.list_modules) {
    return true;
  }

  if (!options.text_output.empty() && !options.text_stream) {
    std::cerr << "--text-output requires --text-stream\n";
    return false;
  }

  if (options.no_console) {
    options.text_stream = false;
    options.text_output.clear();
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

  std::string error_text;

  auto create_decoder = [&](std::unique_ptr<IDecoder>& out_decoder, std::size_t& out_frame_size) -> bool {
    error_text.clear();
    if (!factory->Create(decoder_spec, out_decoder, out_frame_size, error_text)) {
      std::cerr << "Failed to Create decoder '" << decoder_name << "': " << error_text << "\n";
      return false;
    }
    if (out_decoder == nullptr || out_frame_size == 0) {
      std::cerr << "Decoder factory returned invalid decoder/frame size\n";
      return false;
    }
    return true;
  };

  auto add_analysis_sink = [&](IDecoder* decoder_ptr, std::vector<std::unique_ptr<IEventSink>>& sinks) -> bool {
    static_cast<void>(decoder_ptr);
    if (options.analyses.empty()) {
      return true;
    }
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
        return false;
      }

      const IMonitorRealtimeAnalysisFactory* analysis_factory = FindRealtimeAnalysisFactory(analysis_name);
      if (analysis_factory == nullptr) {
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
        return false;
      }

      const std::string expected_decoder =
          analysis_factory->ExpectedDecoder() == nullptr ? "" : analysis_factory->ExpectedDecoder();
      if (!expected_decoder.empty() && expected_decoder != decoder_name) {
        std::cerr << "Analysis '" << analysis_name << "' requires decoder '" << expected_decoder
                  << "', but selected decoder is '" << decoder_name << "'\n";
        return false;
      }

      ParsedAnalysisSpec parsed_spec;
      if (!parsed_spec.Parse(analysis_spec, error_text)) {
        std::cerr << "Invalid spec for analysis '" << analysis_name << "': " << error_text << "\n";
        return false;
      }

      std::unique_ptr<IRealtimeAnalysis> analysis;
      if (!analysis_factory->Create(parsed_spec, analysis, error_text)) {
        std::cerr << "Failed to Create analysis '" << analysis_name << "': " << error_text << "\n";
        return false;
      }
      if (!analysis) {
        std::cerr << "Analysis factory returned null analysis: " << analysis_name << "\n";
        return false;
      }
      analyses.push_back(std::move(analysis));
      analysis_names.push_back(analysis_name);
    }
    RealtimeAnalysisSink::Options sink_options;
    sink_options.enable_gui = !options.no_gui;
    sink_options.redraw_only_on_finalise = !options.input_files.empty();
    sink_options.stop_requested = &g_stop_requested;
    sink_options.snapshot_dir = options.snapshot_dir;
    sink_options.snapshot_interval_ms = options.snapshot_interval_ms;
    sink_options.snapshot_select_endpoint = options.snapshot_select_endpoint;
    sinks.push_back(
        std::make_unique<RealtimeAnalysisSink>(std::move(analyses), std::move(analysis_names), std::move(sink_options)));
    return true;
#else
    std::cerr << "This build does not support realtime analysis. Rebuild with ROOT installed.\n";
    return false;
#endif
  };

  const bool per_input_root_mode = options.root_out_requested && options.root_out.empty();
  uint64_t total_processed_events = 0;

  if (per_input_root_mode) {
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
    std::error_code ec;
    std::filesystem::create_directories(options.dec_dir, ec);
    if (ec) {
      std::cerr << "Failed to create output directory: " << options.dec_dir << " (" << ec.message() << ")\n";
      return 1;
    }

    for (const auto& input_path : options.input_files) {
      std::unique_ptr<IDecoder> decoder;
      std::size_t frame_size = 0;
      if (!create_decoder(decoder, frame_size)) {
        return 1;
      }

      std::vector<std::unique_ptr<IEventSink>> sinks;
      if (options.text_stream) {
        const std::string out = options.text_output.empty() ? "-" : options.text_output;
        sinks.push_back(std::make_unique<TextSink>(decoder.get(), out, 1, false, false));
      }
      sinks.push_back(std::make_unique<RootTreeSink>(decoder.get(), make_per_input_root_path(options.dec_dir, input_path)));
      if (!add_analysis_sink(decoder.get(), sinks)) {
        return 1;
      }
      if (sinks.empty()) {
        std::cerr << "No output sink selected.\n";
        return 1;
      }

      std::unique_ptr<IFrameSource> source = std::make_unique<FileFrameSource>(input_path, frame_size);
      MonitorPipeline pipeline(std::move(source), std::move(decoder), std::move(sinks));
      if (!options.no_console) {
        std::cout << "opening input file: " << input_path << "\n";
      }
      error_text.clear();
      uint64_t processed_events = 0;
      if (!pipeline.Run(options.max_events, error_text, &g_stop_requested, &processed_events)) {
        std::cerr << "datamon failed for '" << input_path << "': " << error_text << "\n";
        return 1;
      }
      total_processed_events += processed_events;
      if (!options.no_console) {
        std::cout << "processed events: file=" << processed_events << " total=" << total_processed_events << "\n";
      }
    }
#else
    std::cerr << "This build does not support ROOT output. Rebuild with ROOT installed.\n";
    return 1;
#endif
  } else {
    std::unique_ptr<IDecoder> decoder;
    std::size_t frame_size = 0;
    if (!create_decoder(decoder, frame_size)) {
      return 1;
    }

    std::vector<std::unique_ptr<IEventSink>> sinks;
    const bool endpoint_source = options.input_files.empty();
    if (options.text_stream) {
      const std::string out = options.text_output.empty() ? "-" : options.text_output;
      sinks.push_back(std::make_unique<TextSink>(decoder.get(), out, 1, false, endpoint_source));
    }
    if (options.root_out_requested && !options.root_out.empty()) {
#if defined(SIMPLEDAQ_HAS_ROOT) && SIMPLEDAQ_HAS_ROOT
      sinks.push_back(std::make_unique<RootTreeSink>(decoder.get(), options.root_out));
#else
      std::cerr << "This build does not support ROOT output. Rebuild with ROOT installed.\n";
      return 1;
#endif
    }
    if (!add_analysis_sink(decoder.get(), sinks)) {
      return 1;
    }
    if (sinks.empty()) {
      std::cerr << "No output sink selected. Enable console output, --root-out, --text-stream, or --analysis.\n";
      return 1;
    }

    std::unique_ptr<IFrameSource> source;
    if (!options.input_files.empty()) {
      if (!options.no_console) {
        for (const auto& input_path : options.input_files) {
          std::cout << "opening input file: " << input_path << "\n";
        }
      }
      if (options.input_files.size() == 1) {
        source = std::make_unique<FileFrameSource>(options.input_files.front(), frame_size);
      } else {
        source = std::make_unique<MultiFileFrameSource>(options.input_files, frame_size);
      }
    } else {
      source = std::make_unique<ZmqDataFrameSource>(options.data_endpoint, options.poll_ms, options.idle_timeout_sec);
    }
    MonitorPipeline pipeline(std::move(source), std::move(decoder), std::move(sinks));
    error_text.clear();
    uint64_t processed_events = 0;
    if (!pipeline.Run(options.max_events, error_text, &g_stop_requested, &processed_events)) {
      std::cerr << "datamon failed: " << error_text << "\n";
      return 1;
    }
    total_processed_events += processed_events;
    if (!options.no_console && !options.input_files.empty()) {
      std::cout << "processed events: total=" << total_processed_events << "\n";
    }
  }

  return 0;
}
