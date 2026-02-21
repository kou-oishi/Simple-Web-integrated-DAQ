#include <csignal>
#include <cctype>
#include <chrono>
#include <array>
#include <cstring>
#include <cerrno>
#include <getopt.h>
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

namespace {

volatile std::sig_atomic_t g_terminate = 0;

void handle_signal(int /*signum*/) { g_terminate = 1; }

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

bool parse_config_from_args_text(const std::string& args_text, DaqConfig& cfg, std::string& err) {
  std::istringstream iss(args_text);
  std::vector<std::string> parts;
  std::string tok;
  while (iss >> tok) {
    parts.push_back(tok);
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
    return "ok " + status_fields_locked();
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

  std::string start(const std::string& args_text) {
    DaqConfig cfg;
    std::string err;
    if (!parse_config_from_args_text(args_text, cfg, err)) {
      return "error " + err;
    }

    {
      std::lock_guard<std::mutex> lock(mu_);
      if (running_) {
        return "error already running";
      }
      stop_requested_ = 0;
      running_ = true;
      state_ = "running";
      last_error_.clear();
      active_cfg_ = cfg;
      published_events_ = 0;
      events_in_current_run_ = 0;
      last_source_.clear();
      started_at_ = std::chrono::steady_clock::now();
    }

    worker_ = std::thread([this, cfg]() {
      const int rc = RunDaqCore(cfg, stop_requested_, [this](const FrameRecord& rec) {
        on_frame_published(rec);
        if (frame_cb_) {
          frame_cb_(rec);
        }
      });
      std::lock_guard<std::mutex> lock(mu_);
      running_ = false;
      last_exit_code_ = rc;
      state_ = (rc == 0) ? "idle" : "error";
      if (rc != 0) {
        last_error_ = "daq exited with non-zero";
      }
    });

    return "ok started";
  }

  std::string stop() {
    std::thread t;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (!running_) {
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

    return "ok stopped";
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
  bool is_running() const {
    std::lock_guard<std::mutex> lock(mu_);
    return running_;
  }

  std::string status_fields_locked() const {
    std::ostringstream oss;
    oss << "state=" << state_
        << " running=" << (running_ ? 1 : 0)
        << " healthy=" << (state_ == "error" ? 0 : 1)
        << " last_exit=" << last_exit_code_
        << " events_total=" << published_events_
        << " run=" << current_run_number_locked()
        << " events_in_run=" << events_in_current_run_;
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
    if (active_cfg_.events_per_file == 0) {
      return active_cfg_.run_start;
    }
    return active_cfg_.run_start + static_cast<uint32_t>(published_events_ / active_cfg_.events_per_file);
  }

  void on_frame_published(const FrameRecord& rec) {
    std::lock_guard<std::mutex> lock(mu_);
    ++published_events_;
    last_source_ = rec.source;
    if (active_cfg_.events_per_file == 0) {
      events_in_current_run_ = published_events_;
      return;
    }
    events_in_current_run_ = published_events_ % active_cfg_.events_per_file;
    if (events_in_current_run_ == 0) {
      events_in_current_run_ = active_cfg_.events_per_file;
    }
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
  const DataFrameHeader header{.run_number = rec.run_number, .event_number = rec.event_number};
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
            << " [--endpoint <zmq-endpoint>] [--status-endpoint <zmq-endpoint>] [--data-endpoint <zmq-endpoint>]\n";
  std::cerr << "Options:\n";
  std::cerr << "  -e, --endpoint <ep>         Control REP endpoint to bind\n";
  std::cerr << "  -s, --status-endpoint <ep>  Status PUB endpoint to bind\n";
  std::cerr << "  -d, --data-endpoint <ep>    Data PUB endpoint to bind\n";
  std::cerr << "  -h, --help                  Show this help\n";
  std::cerr << "Default control endpoint: " << daq_defaults::kControlEndpoint << "\n";
  std::cerr << "Default status endpoint:  " << daq_defaults::kStatusEndpoint << "\n";
  std::cerr << "Default data endpoint:    " << daq_defaults::kDataEndpoint << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string endpoint = daq_defaults::kControlEndpoint;
  std::string status_endpoint = daq_defaults::kStatusEndpoint;
  std::string data_endpoint = daq_defaults::kDataEndpoint;

  static constexpr option kLongOpts[] = {
      {"endpoint", required_argument, nullptr, 'e'},
      {"status-endpoint", required_argument, nullptr, 's'},
      {"data-endpoint", required_argument, nullptr, 'd'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0},
  };

  optind = 1;
  opterr = 0;
  while (true) {
    const int c = ::getopt_long(argc, argv, ":e:s:d:h", kLongOpts, nullptr);
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

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  void* ctx = zmq_ctx_new();
  if (ctx == nullptr) {
    std::cerr << "zmq_ctx_new failed\n";
    return 1;
  }

  void* rep = zmq_socket(ctx, ZMQ_REP);
  if (rep == nullptr) {
    std::cerr << "zmq_socket(ZMQ_REP) failed\n";
    zmq_ctx_term(ctx);
    return 1;
  }

  const int rcv_timeout_ms = 200;
  zmq_setsockopt(rep, ZMQ_RCVTIMEO, &rcv_timeout_ms, sizeof(rcv_timeout_ms));

  if (zmq_bind(rep, endpoint.c_str()) != 0) {
    std::cerr << "zmq_bind failed: " << endpoint << "\n";
    zmq_close(rep);
    zmq_ctx_term(ctx);
    return 1;
  }

  void* pub = zmq_socket(ctx, ZMQ_PUB);
  if (pub == nullptr) {
    std::cerr << "zmq_socket(ZMQ_PUB) failed\n";
    zmq_close(rep);
    zmq_ctx_term(ctx);
    return 1;
  }

  if (zmq_bind(pub, status_endpoint.c_str()) != 0) {
    std::cerr << "zmq_bind failed: " << status_endpoint << "\n";
    zmq_close(pub);
    zmq_close(rep);
    zmq_ctx_term(ctx);
    return 1;
  }

  void* pub_data = zmq_socket(ctx, ZMQ_PUB);
  if (pub_data == nullptr) {
    std::cerr << "zmq_socket(ZMQ_PUB) for data failed\n";
    zmq_close(pub);
    zmq_close(rep);
    zmq_ctx_term(ctx);
    return 1;
  }

  if (zmq_bind(pub_data, data_endpoint.c_str()) != 0) {
    std::cerr << "zmq_bind failed: " << data_endpoint << "\n";
    zmq_close(pub_data);
    zmq_close(pub);
    zmq_close(rep);
    zmq_ctx_term(ctx);
    return 1;
  }

  std::cerr << "[INFO] daqd control at " << endpoint << "\n";
  std::cerr << "[INFO] daqd status at  " << status_endpoint << "\n";
  std::cerr << "[INFO] daqd data at    " << data_endpoint << "\n";

  DaqService svc([&](const FrameRecord& rec) { publish_data(pub_data, rec); });
  std::string last_state = svc.state_name();
  uint32_t last_run = svc.current_run_number();
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
      } else if (cmd == "stop") {
        resp = svc.stop();
      } else if (cmd == "shutdown") {
        svc.shutdown();
        resp = "ok shutting down";
      } else {
        resp = "error unknown command";
      }

      zmq_send(rep, resp.data(), resp.size(), 0);
    } else {
      zmq_msg_close(&msg);
    }

    const std::string state = svc.state_name();
    const uint32_t run = svc.current_run_number();
    const auto now = std::chrono::steady_clock::now();
    const bool periodic = (now - last_status_pub) >= std::chrono::seconds(1);
    if (state != last_state || run != last_run || periodic) {
      last_state = state;
      last_run = run;
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
