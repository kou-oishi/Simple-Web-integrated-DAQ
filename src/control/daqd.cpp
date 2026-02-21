#include <csignal>
#include <cctype>
#include <chrono>
#include <array>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <zmq.h>

#include "core/daq_cli.hpp"
#include "core/data_frame_header.hpp"
#include "core/daq_runtime.hpp"
#include "core/defaults.hpp"
#include "core/mysql_logger.hpp"
#include "concrete_modules/module_registry.hpp"

namespace {

volatile std::sig_atomic_t g_terminate = 0;
std::mutex g_log_mutex;

void handle_signal(int /*signum*/) { g_terminate = 1; }

void log_line(const std::string& text) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  const auto now = std::chrono::system_clock::now();
  const std::time_t raw_time = std::chrono::system_clock::to_time_t(now);
  std::tm tm_value{};
#if defined(_WIN32)
  localtime_s(&tm_value, &raw_time);
#else
  localtime_r(&raw_time, &tm_value);
#endif
  std::cerr << "[" << std::put_time(&tm_value, "%Y-%m-%d %H:%M:%S") << "] " << text << "\n";
}

std::string trim(const std::string& s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])) != 0) {
    ++b;
  }
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) {
    --e;
  }
  return s.substr(b, e - b);
}

std::pair<std::string, std::string> split_command(const std::string& req) {
  const std::string t = trim(req);
  const size_t sp = t.find(' ');
  if (sp == std::string::npos) {
    return {t, ""};
  }
  return {t.substr(0, sp), trim(t.substr(sp + 1))};
}

std::string to_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string json_escape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (const char c : text) {
    switch (c) {
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
        out.push_back(c);
        break;
    }
  }
  return out;
}

std::string field_type_text(DeviceFrontendFieldType type) {
  switch (type) {
    case DeviceFrontendFieldType::kInteger:
      return "integer";
    case DeviceFrontendFieldType::kIpv4:
      return "ipv4";
    case DeviceFrontendFieldType::kString:
    default:
      return "string";
  }
}

std::string frontend_schema_json() {
  std::ostringstream oss;
  oss << "{\"frontends\":[";
  const auto ids = ListDeviceFrontendIds();
  bool first_frontend = true;
  for (size_t i = 0; i < ids.size(); ++i) {
    const IDeviceFrontend* frontend = FindDeviceFrontend(ids[i]);
    if (frontend == nullptr) {
      continue;
    }
    const DeviceFrontendSchema schema = frontend->DescribeDeviceSpec();
    if (!first_frontend) {
      oss << ",";
    }
    first_frontend = false;
    oss << "{\"id\":\"" << json_escape(ids[i]) << "\"";
    oss << ",\"spec_format\":\"" << json_escape(schema.spec_format) << "\"";
    oss << ",\"spec_template\":\"" << json_escape(schema.spec_template) << "\"";
    oss << ",\"fields\":[";
    for (size_t f = 0; f < schema.fields.size(); ++f) {
      const auto& field = schema.fields[f];
      if (f != 0) {
        oss << ",";
      }
      oss << "{\"name\":\"" << json_escape(field.name) << "\"";
      oss << ",\"type\":\"" << field_type_text(field.type) << "\"";
      oss << ",\"required\":" << (field.required ? "true" : "false");
      if (field.has_min) {
        oss << ",\"min\":" << field.min_value;
      }
      if (field.has_max) {
        oss << ",\"max\":" << field.max_value;
      }
      if (!field.description.empty()) {
        oss << ",\"description\":\"" << json_escape(field.description) << "\"";
      }
      oss << "}";
    }
    oss << "]}";
  }
  oss << "]}";
  return oss.str();
}

bool parse_config_from_args_text(const std::string& args_text, DaqConfig& cfg, std::string& err) {
  auto split_args = [](const std::string& text, std::vector<std::string>& out_args) -> bool {
    out_args.clear();
    std::string current;
    bool in_quotes = false;
    bool escaping = false;
    for (const char c : text) {
      if (escaping) {
        current.push_back(c);
        escaping = false;
        continue;
      }
      if (c == '\\') {
        escaping = true;
        continue;
      }
      if (c == '"') {
        in_quotes = !in_quotes;
        continue;
      }
      if (!in_quotes && std::isspace(static_cast<unsigned char>(c)) != 0) {
        if (!current.empty()) {
          out_args.push_back(current);
          current.clear();
        }
        continue;
      }
      current.push_back(c);
    }

    if (escaping || in_quotes) {
      return false;
    }
    if (!current.empty()) {
      out_args.push_back(current);
    }
    return true;
  };

  std::vector<std::string> parts;
  if (!split_args(args_text, parts)) {
    err = "invalid quoted argument in start command";
    return false;
  }

  std::vector<std::string> argv_store;
  argv_store.reserve(parts.size() + 1);
  argv_store.push_back("daq_core");
  for (const auto& p : parts) {
    argv_store.push_back(p);
  }

  std::vector<char*> argv;
  argv.reserve(argv_store.size());
  for (auto& s : argv_store) {
    argv.push_back(s.data());
  }

  if (!ParseDaqArgs(static_cast<int>(argv.size()), argv.data(), cfg)) {
    err = "invalid start arguments";
    return false;
  }
  return true;
}

class DaqService {
 public:
  explicit DaqService(FramePublishCallback frame_cb) : frame_cb_(std::move(frame_cb)) {}

  ~DaqService() { shutdown(); }

  std::string status() const {
    std::lock_guard<std::mutex> lock(mu_);
    return "Ok " + status_fields_locked();
  }

  std::string status_fields() const {
    std::lock_guard<std::mutex> lock(mu_);
    return status_fields_locked();
  }

  std::string state_name() const {
    std::lock_guard<std::mutex> lock(mu_);
    return state_;
  }

  uint32_t current_run_number() const {
    std::lock_guard<std::mutex> lock(mu_);
    return current_run_number_locked();
  }

  uint32_t current_subrun_number() const {
    std::lock_guard<std::mutex> lock(mu_);
    return current_subrun_number_locked();
  }

  std::string start(const std::string& args_text) {
    reap_finished_worker();

    DaqConfig cfg;
    std::string err;
    if (!parse_config_from_args_text(args_text, cfg, err)) {
      return "error " + err;
    }

    MySqlLogger mysql_logger;
    uint32_t resolved_run_number = cfg.run_start;
    if (!mysql_logger.ResolveRunNumber(cfg.run_start_specified, cfg.run_start, resolved_run_number, err)) {
      return "error " + err;
    }
    cfg.run_start = resolved_run_number;
    cfg.run_start_specified = true;

    {
      std::lock_guard<std::mutex> lock(mu_);
      if (running_) {
        return "error already running";
      }
      if (state_ == "paused") {
        return "error currently paused; use resume";
      }
      const std::string start_result = start_locked(cfg, false);
      if (start_result.rfind("Ok", 0) != 0) {
        return start_result;
      }
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(cfg.startup_connect_timeout_sec + 1);
    while (std::chrono::steady_clock::now() < deadline) {
      {
        std::lock_guard<std::mutex> wait_lock(mu_);
        if (!running_) {
          return "error " + (last_error_.empty() ? std::string("startup failed") : last_error_);
        }
        if (startup_status_ready_) {
          if (startup_status_ok_) {
            return "Ok started";
          }
          return "error " + (startup_status_message_.empty() ? std::string("startup failed") : startup_status_message_);
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return "error startup check timed out";
  }

  std::string stop() {
    std::thread t;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (!running_) {
        if (state_ == "paused") {
          state_ = "idle";
          return "Ok stopped";
        }
        return "error not running";
      }
      state_ = "stopping";
      stop_requested_ = 1;
      t = std::move(worker_);
    }

    if (t.joinable()) {
      t.join();
    }

    {
      std::lock_guard<std::mutex> lock(mu_);
      if (!running_ && state_ == "stopping") {
        state_ = (last_exit_code_ == 0) ? "idle" : "error";
      }
    }

    return "Ok stopped";
  }

  std::string pause() {
    std::thread t;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (!running_) {
        if (state_ == "paused") {
          return "error already paused";
        }
        return "error not running";
      }
      state_ = "pausing";
      stop_requested_ = 2;
      t = std::move(worker_);
    }

    if (t.joinable()) {
      t.join();
    }

    {
      std::lock_guard<std::mutex> lock(mu_);
      if (last_exit_code_ != 0) {
        state_ = "error";
        if (last_error_.empty()) {
          last_error_ = "pause failed: daq exited with non-zero";
        }
        return "error pause failed";
      }
      if (published_events_ > 0) {
        MySqlLogger mysql_logger;
        std::string mysql_error;
        const uint32_t paused_run = current_run_number_locked();
        const uint32_t paused_subrun = current_subrun_number_locked();
        if (!mysql_logger.UpdateRunLogStatus(paused_run, paused_subrun, "paused", mysql_error)) {
          state_ = "error";
          last_error_ = "failed to update paused status in MySQL: " + mysql_error;
          return "error " + last_error_;
        }
      }
      active_cfg_.subrun_start = current_subrun_number_locked() + 1;
      active_cfg_.allow_existing_run = true;
      state_ = "paused";
    }
    return "Ok paused";
  }

  std::string resume() {
    reap_finished_worker();

    {
      std::lock_guard<std::mutex> lock(mu_);
      if (running_) {
        return "error already running";
      }
      if (state_ != "paused") {
        return "error not paused";
      }
      const std::string start_result = start_locked(active_cfg_, true);
      if (start_result.rfind("Ok", 0) != 0) {
        return start_result;
      }
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(active_cfg_.startup_connect_timeout_sec + 1);
    while (std::chrono::steady_clock::now() < deadline) {
      {
        std::lock_guard<std::mutex> wait_lock(mu_);
        if (!running_) {
          return "error " + (last_error_.empty() ? std::string("resume failed") : last_error_);
        }
        if (startup_status_ready_) {
          if (startup_status_ok_) {
            return "Ok resumed";
          }
          return "error " + (startup_status_message_.empty() ? std::string("resume failed") : startup_status_message_);
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return "error resume check timed out";
  }

  void shutdown() {
    if (is_running()) {
      (void)stop();
    }
    std::lock_guard<std::mutex> lock(mu_);
    shutting_down_ = true;
  }

  bool is_shutting_down() const {
    std::lock_guard<std::mutex> lock(mu_);
    return shutting_down_;
  }

 private:
  void reap_finished_worker() {
    std::thread finished;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (running_ || !worker_.joinable()) {
        return;
      }
      finished = std::move(worker_);
    }
    if (finished.joinable()) {
      finished.join();
    }
  }

  bool is_running() const {
    std::lock_guard<std::mutex> lock(mu_);
    return running_;
  }

  std::string status_fields_locked() const {
    std::ostringstream oss;
    const uint32_t events_per_file = active_cfg_.events_per_file;
    uint32_t events_in_file = 0;
    if (events_per_file > 0 && published_events_ > 0) {
      events_in_file = static_cast<uint32_t>(((published_events_ - 1) % events_per_file) + 1);
    }
    oss << "state=" << state_
        << " running=" << (running_ ? 1 : 0)
        << " healthy=" << (state_ == "error" ? 0 : 1)
        << " last_exit=" << last_exit_code_
        << " events_total=" << published_events_
        << " Run=" << current_run_number_locked()
        << " subrun=" << current_subrun_number_locked()
        << " events_in_run=" << events_in_current_run_
        << " events_per_file=" << events_per_file
        << " events_in_file=" << events_in_file;
    if (running_) {
      const auto elapsed = std::chrono::steady_clock::now() - started_at_;
      const auto sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
      oss << " uptime_sec=" << sec;
    }
    if (!last_source_.empty()) {
      oss << " last_source=" << last_source_;
    }
    if (!last_error_.empty()) {
      oss << " error=" << last_error_;
    }
    return oss.str();
  }

  uint32_t current_run_number_locked() const {
    return active_cfg_.run_start;
  }

  uint32_t current_subrun_number_locked() const {
    if (published_events_ == 0 || active_cfg_.events_per_file == 0) {
      return active_cfg_.subrun_start;
    }
    return active_cfg_.subrun_start +
           static_cast<uint32_t>((published_events_ - 1) / active_cfg_.events_per_file);
  }

  void on_frame_published(const FrameRecord& rec) {
    std::lock_guard<std::mutex> lock(mu_);
    ++published_events_;
    last_source_ = rec.source;
    events_in_current_run_ = published_events_;
  }

  std::string start_locked(const DaqConfig& cfg, bool is_resume) {
    stop_requested_ = 0;
    running_ = true;
    state_ = "running";
    last_error_.clear();
    active_cfg_ = cfg;
    active_cfg_.allow_existing_run = is_resume;
    published_events_ = 0;
    events_in_current_run_ = 0;
    last_source_.clear();
    started_at_ = std::chrono::steady_clock::now();
    startup_status_ready_ = false;
    startup_status_ok_ = false;
    startup_status_message_.clear();

    worker_ = std::thread([this, cfg]() {
      const int rc = RunDaqCore(
          cfg,
          stop_requested_,
          [this](const FrameRecord& rec) {
            on_frame_published(rec);
            if (frame_cb_) {
              frame_cb_(rec);
            }
          },
          [this](bool ok, const std::string& message) {
            std::lock_guard<std::mutex> status_lock(mu_);
            startup_status_ready_ = true;
            startup_status_ok_ = ok;
            startup_status_message_ = message;
            if (!ok) {
              last_error_ = message;
            }
          },
          [this](const std::string& message) {
            std::lock_guard<std::mutex> status_lock(mu_);
            last_error_ = message;
          });
      std::lock_guard<std::mutex> lock(mu_);
      running_ = false;
      last_exit_code_ = rc;
      state_ = (rc == 0) ? "idle" : "error";
      if (rc != 0) {
        if (last_error_.empty()) {
          last_error_ = "daq exited with non-zero";
        }
      }
    });
    return "Ok";
  }

  mutable std::mutex mu_;
  std::thread worker_;
  volatile std::sig_atomic_t stop_requested_ = 0;
  bool running_ = false;
  bool shutting_down_ = false;
  int last_exit_code_ = 0;
  std::string state_ = "idle";
  std::string last_error_;
  DaqConfig active_cfg_;
  uint64_t published_events_ = 0;
  uint64_t events_in_current_run_ = 0;
  std::string last_source_;
  std::chrono::steady_clock::time_point started_at_ = std::chrono::steady_clock::now();
  bool startup_status_ready_ = false;
  bool startup_status_ok_ = false;
  std::string startup_status_message_;
  FramePublishCallback frame_cb_;
};

void publish_status(void* pub_sock, const std::string& payload) {
  const std::string topic = "status";
  zmq_send(pub_sock, topic.data(), topic.size(), ZMQ_SNDMORE);
  zmq_send(pub_sock, payload.data(), payload.size(), 0);
}

void publish_data(void* pub_sock, const FrameRecord& rec) {
  const std::string topic = "data";
  std::array<uint8_t, DataFrameHeader::kWireSize> header_wire{};
  const DataFrameHeader header{
      .run_number = rec.run_number,
      .subrun_number = rec.subrun_number,
      .event_number = rec.event_number,
  };
  WriteDataFrameHeader(header, header_wire);

  const int flags_more = ZMQ_SNDMORE | ZMQ_DONTWAIT;
  const int flags_last = ZMQ_DONTWAIT;

  if (zmq_send(pub_sock, topic.data(), topic.size(), flags_more) < 0) {
    return;
  }
  if (zmq_send(pub_sock, rec.source.data(), rec.source.size(), flags_more) < 0) {
    return;
  }
  if (zmq_send(pub_sock, header_wire.data(), header_wire.size(), flags_more) < 0) {
    return;
  }
  if (zmq_send(pub_sock, rec.payload.data(), rec.payload.size(), flags_last) < 0) {
    if (errno == EAGAIN) {
      return;
    }
  }
}

void print_usage(const char* prog) {
  std::cerr << "Usage: " << prog
            << " [--endpoint <zmq-endpoint>] [--status-endpoint <zmq-endpoint>] [--data-endpoint <zmq-endpoint>]"
            << " [--log-file <path>]\n";
  std::cerr << "Options:\n";
  std::cerr << "  -e, --endpoint <ep>         Control REP endpoint to bind\n";
  std::cerr << "  -s, --status-endpoint <ep>  Status PUB endpoint to bind\n";
  std::cerr << "  -d, --data-endpoint <ep>    Data PUB endpoint to bind\n";
  std::cerr << "  -l, --log-file <path>       daqd log file path\n";
  std::cerr << "  -h, --help                  Show this help\n";
  std::cerr << "Default control endpoint: " << daq_defaults::kControlEndpoint << "\n";
  std::cerr << "Default status endpoint:  " << daq_defaults::kStatusEndpoint << "\n";
  std::cerr << "Default data endpoint:    " << daq_defaults::kDataEndpoint << "\n";
  std::cerr << "Default log file:         " << daq_defaults::kDaqdLogPath << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string endpoint = daq_defaults::kControlEndpoint;
  std::string status_endpoint = daq_defaults::kStatusEndpoint;
  std::string data_endpoint = daq_defaults::kDataEndpoint;
  std::string log_file_path = daq_defaults::kDaqdLogPath;

  static constexpr option kLongOpts[] = {
      {"endpoint", required_argument, nullptr, 'e'},
      {"status-endpoint", required_argument, nullptr, 's'},
      {"data-endpoint", required_argument, nullptr, 'd'},
      {"log-file", required_argument, nullptr, 'l'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0},
  };

  optind = 1;
  opterr = 0;
  while (true) {
    const int c = ::getopt_long(argc, argv, ":e:s:d:l:h", kLongOpts, nullptr);
    if (c == -1) {
      break;
    }
    switch (c) {
      case 'e':
        endpoint = optarg;
        break;
      case 's':
        status_endpoint = optarg;
        break;
      case 'd':
        data_endpoint = optarg;
        break;
      case 'l':
        log_file_path = optarg;
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

  std::ofstream log_file;
  std::streambuf* original_cerr = std::cerr.rdbuf();
  struct CerrRestoreGuard {
    explicit CerrRestoreGuard(std::streambuf* old) : old_(old) {}
    ~CerrRestoreGuard() { std::cerr.rdbuf(old_); }
    std::streambuf* old_ = nullptr;
  } cerr_restore(original_cerr);
  std::streambuf* log_sink = original_cerr;
  if (!log_file_path.empty()) {
    const std::filesystem::path log_path(log_file_path);
    std::error_code mkdir_error;
    if (log_path.has_parent_path()) {
      std::filesystem::create_directories(log_path.parent_path(), mkdir_error);
    }
    log_file.open(log_path, std::ios::app);
    if (!log_file.is_open()) {
      log_line("[WARN] failed to open log file: " + log_path.string());
    } else {
      log_sink = log_file.rdbuf();
    }
  }
  std::cerr.rdbuf(log_sink);

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  void* ctx = zmq_ctx_new();
  if (ctx == nullptr) {
    log_line("[ERROR] zmq_ctx_new failed");
    return 1;
  }

  void* rep = zmq_socket(ctx, ZMQ_REP);
  if (rep == nullptr) {
    log_line("[ERROR] zmq_socket(ZMQ_REP) failed");
    zmq_ctx_term(ctx);
    return 1;
  }

  const int rcv_timeout_ms = 200;
  zmq_setsockopt(rep, ZMQ_RCVTIMEO, &rcv_timeout_ms, sizeof(rcv_timeout_ms));

  if (zmq_bind(rep, endpoint.c_str()) != 0) {
    log_line("[ERROR] zmq_bind failed: " + endpoint);
    zmq_close(rep);
    zmq_ctx_term(ctx);
    return 1;
  }

  void* pub = zmq_socket(ctx, ZMQ_PUB);
  if (pub == nullptr) {
    log_line("[ERROR] zmq_socket(ZMQ_PUB) failed");
    zmq_close(rep);
    zmq_ctx_term(ctx);
    return 1;
  }

  if (zmq_bind(pub, status_endpoint.c_str()) != 0) {
    log_line("[ERROR] zmq_bind failed: " + status_endpoint);
    zmq_close(pub);
    zmq_close(rep);
    zmq_ctx_term(ctx);
    return 1;
  }

  void* pub_data = zmq_socket(ctx, ZMQ_PUB);
  if (pub_data == nullptr) {
    log_line("[ERROR] zmq_socket(ZMQ_PUB) for data failed");
    zmq_close(pub);
    zmq_close(rep);
    zmq_ctx_term(ctx);
    return 1;
  }

  if (zmq_bind(pub_data, data_endpoint.c_str()) != 0) {
    log_line("[ERROR] zmq_bind failed: " + data_endpoint);
    zmq_close(pub_data);
    zmq_close(pub);
    zmq_close(rep);
    zmq_ctx_term(ctx);
    return 1;
  }

  log_line("[INFO] daqd control at " + endpoint);
  log_line("[INFO] daqd status at  " + status_endpoint);
  log_line("[INFO] daqd data at    " + data_endpoint);

  DaqService svc([&](const FrameRecord& rec) { publish_data(pub_data, rec); });
  std::string last_state = svc.state_name();
  uint32_t last_run = svc.current_run_number();
  uint32_t last_subrun = svc.current_subrun_number();
  auto last_status_pub = std::chrono::steady_clock::now();
  publish_status(pub, svc.status_fields());

  while (g_terminate == 0 && !svc.is_shutting_down()) {
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    const int n = static_cast<int>(zmq_msg_recv(&msg, rep, 0));
    if (n >= 0) {
      const char* data = static_cast<const char*>(zmq_msg_data(&msg));
      const size_t size = zmq_msg_size(&msg);
      const std::string req(data, data + size);
      zmq_msg_close(&msg);

      const auto [cmd_raw, rest] = split_command(req);
      const std::string cmd = to_lower(cmd_raw);

      std::string resp;
      if (cmd == "status") {
        resp = svc.status();
      } else if (cmd == "start") {
        resp = svc.start(rest);
      } else if (cmd == "pause") {
        resp = svc.pause();
      } else if (cmd == "resume") {
        resp = svc.resume();
      } else if (cmd == "stop") {
        resp = svc.stop();
      } else if (cmd == "shutdown") {
        svc.shutdown();
        resp = "Ok shutting down";
      } else if (cmd == "frontends") {
        resp = "Ok " + frontend_schema_json();
      } else {
        resp = "error unknown command";
      }

      zmq_send(rep, resp.data(), resp.size(), 0);
    } else {
      zmq_msg_close(&msg);
    }

    const std::string state = svc.state_name();
    const uint32_t Run = svc.current_run_number();
    const uint32_t subrun = svc.current_subrun_number();
    const auto now = std::chrono::steady_clock::now();
    const bool periodic = (now - last_status_pub) >= std::chrono::seconds(1);
    if (state != last_state || Run != last_run || subrun != last_subrun || periodic) {
      last_state = state;
      last_run = Run;
      last_subrun = subrun;
      last_status_pub = now;
      publish_status(pub, svc.status_fields());
    }
  }

  svc.shutdown();
  publish_status(pub, svc.status_fields());
  zmq_close(pub_data);
  zmq_close(pub);
  zmq_close(rep);
  zmq_ctx_term(ctx);
  return 0;
}
