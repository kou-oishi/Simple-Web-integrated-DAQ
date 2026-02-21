#include "core/daq_runtime.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <ctime>

#include "core/blocking_queue.hpp"
#include "core/defaults.hpp"
#include "concrete_modules/module_registry.hpp"
#include "core/mysql_logger.hpp"
#include "core/validation_result.hpp"

namespace {

std::mutex g_log_mutex;

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

const char* to_string(ValidationResult::Status s) {
  switch (s) {
    case ValidationResult::Status::kOk:
      return "Ok";
    case ValidationResult::Status::kRecoverableError:
      return "Recoverable";
    case ValidationResult::Status::kFatalError:
      return "Fatal";
  }
  return "unknown";
}

std::string device_label(const DeviceSpec& spec) {
  return spec.frontend + "#" + std::to_string(static_cast<unsigned>(spec.board_id)) + "@" + spec.host + ":" +
         std::to_string(spec.port);
}

void run_worker(const DeviceSpec& spec,
                size_t worker_index,
                const DaqConfig& cfg,
                BlockingQueue<FrameRecord>& queue,
                std::atomic<bool>& running,
                volatile std::sig_atomic_t& stop_requested,
                const std::chrono::steady_clock::time_point& startup_deadline,
                std::vector<std::atomic<bool>>& connected_once,
                std::atomic<uint32_t>& connected_count,
                std::atomic<bool>& startup_failed,
                std::once_flag& startup_notify_once,
                const StartupStatusCallback& on_startup_status,
                const RuntimeErrorCallback& on_runtime_error) {
  const IDeviceFrontend* frontend = FindDeviceFrontend(spec.frontend);
  if (frontend == nullptr) {
    log_line("[ERROR] unknown frontend in runtime: " + spec.frontend);
    startup_failed.store(true);
    running.store(false);
    queue.Close();
    return;
  }

  std::unique_ptr<IDeviceDriver> driver = frontend->CreateDriver(spec);
  std::unique_ptr<IDataValidator> validator = frontend->CreateValidator(spec);
  if (!driver || !validator) {
    log_line("[ERROR] failed to create driver/validator for frontend: " + spec.frontend);
    startup_failed.store(true);
    running.store(false);
    queue.Close();
    return;
  }

  bool reconnect_window_active = false;
  std::chrono::steady_clock::time_point reconnect_deadline{};

  while (running.load() && stop_requested == 0) {
    if (!driver->ConnectDevice()) {
      log_line("[WARN] Connect failed: " + device_label(spec));
      if (!connected_once[worker_index].load() &&
          std::chrono::steady_clock::now() >= startup_deadline) {
        const std::string msg = "startup failed: cannot connect " + device_label(spec);
        log_line("[ERROR] " + msg);
        startup_failed.store(true);
        std::call_once(startup_notify_once, [&]() {
          if (on_startup_status) {
            on_startup_status(false, msg);
          }
        });
        running.store(false);
        queue.Close();
        return;
      }
      if (connected_once[worker_index].load()) {
        const auto now = std::chrono::steady_clock::now();
        if (!reconnect_window_active) {
          reconnect_window_active = true;
          reconnect_deadline = now + std::chrono::seconds(cfg.reconnect_failure_timeout_sec);
        } else if (now >= reconnect_deadline) {
          const std::string msg =
              "runtime failed: reconnect timeout for " + device_label(spec);
          log_line("[ERROR] " + msg);
          if (on_runtime_error) {
            on_runtime_error(msg);
          }
          startup_failed.store(true);
          running.store(false);
          queue.Close();
          return;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(cfg.reconnect_ms));
      continue;
    }

    reconnect_window_active = false;

    if (!connected_once[worker_index].exchange(true)) {
      const uint32_t n = connected_count.fetch_add(1) + 1;
      if (n == connected_once.size()) {
        std::call_once(startup_notify_once, [&]() {
          if (on_startup_status) {
            on_startup_status(true, "all devices connected");
          }
        });
      }
    }

    log_line("[INFO] connected: " + device_label(spec));

    while (running.load() && stop_requested == 0) {
      std::vector<uint8_t> chunk;
      const ReadStatus st = driver->ReadBytes(
          chunk, daq_defaults::kReadChunkSizeBytes, static_cast<int>(cfg.read_timeout_ms));
      if (st == ReadStatus::kTimeout) {
        continue;
      }
      if (st != ReadStatus::kOk) {
        log_line("[WARN] disconnected/read-error: " + device_label(spec));
        driver->DisconnectDevice();
        validator->Reset();
        if (connected_once[worker_index].load() && !reconnect_window_active) {
          reconnect_window_active = true;
          reconnect_deadline =
              std::chrono::steady_clock::now() + std::chrono::seconds(cfg.reconnect_failure_timeout_sec);
        }
        break;
      }

      std::vector<std::vector<uint8_t>> frames;
      const ValidationResult vr = validator->Feed(chunk.data(), chunk.size(), frames);
      if (!vr.IsOk()) {
        std::ostringstream oss;
        oss << "[ERROR] validator error on " << device_label(spec)
            << " status=" << to_string(vr.status())
            << " code=" << static_cast<uint32_t>(vr.code())
            << " detail_code=" << vr.detail_code()
            << " detail_tag=" << vr.detail_tag()
            << " message=" << vr.message();
        log_line(oss.str());

        if (vr.IsFatal()) {
          startup_failed.store(true);
          running.store(false);
          queue.Close();
          return;
        }

        driver->DisconnectDevice();
        validator->Reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.reconnect_ms));
        break;
      }

      for (auto& frame : frames) {
        FrameRecord rec;
        rec.source = "board" + std::to_string(static_cast<unsigned>(spec.board_id));
        rec.payload = std::move(frame);
        if (!queue.Push(std::move(rec))) {
          return;
        }
      }
    }
  }

  driver->DisconnectDevice();
}

bool run_writer(const DaqConfig& cfg,
                BlockingQueue<FrameRecord>& queue,
                const FramePublishCallback& on_frame_ready,
                const std::atomic<bool>& run_failed) {
  std::error_code ec;
  std::filesystem::create_directories(cfg.output_dir, ec);
  if (ec) {
    log_line("Failed to create output_dir: " + cfg.output_dir + " (" + ec.message() + ")");
    return false;
  }

  auto make_run_path = [&](uint32_t run_number, uint32_t subrun_number) -> std::filesystem::path {
    std::ostringstream oss;
    oss << "run" << std::setw(daq_defaults::kRunNumberWidth) << std::setfill('0') << run_number
        << "_sub" << std::setw(daq_defaults::kRunNumberWidth) << std::setfill('0') << subrun_number
        << ".dat";
    return std::filesystem::path(cfg.output_dir) / oss.str();
  };

  const uint32_t run_number = cfg.run_start;
  uint32_t subrun_number = cfg.subrun_start;
  uint32_t events_in_current_file = 0;
  uint64_t next_event_number = 0;
  std::chrono::system_clock::time_point subrun_start_time{};
  std::ofstream ofs;
  MySqlLogger mysql_logger;

  auto write_run_log = [&](uint32_t subrun,
                           uint64_t event_count,
                           const std::chrono::system_clock::time_point& start_time,
                           const char* status,
                           std::string& out_error_text) -> bool {
    out_error_text.clear();
    if (event_count == 0 || !mysql_logger.IsEnabled()) {
      return true;
    }
    RunLogEntry entry;
    entry.run_number = run_number;
    entry.subrun_number = subrun;
    entry.event_count = event_count;
    entry.start_time = start_time;
    entry.end_time = std::chrono::system_clock::now();
    entry.status = status == nullptr ? "stopped" : status;
    entry.comment = cfg.comment;
    return mysql_logger.InsertRunLog(entry, out_error_text);
  };

  auto open_run_file = [&](uint32_t subrun) -> bool {
    const std::filesystem::path path = make_run_path(run_number, subrun);
    ofs = std::ofstream(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
      log_line("Failed to open output file: " + path.string());
      return false;
    }
    events_in_current_file = 0;
    subrun_start_time = std::chrono::system_clock::now();
    log_line("[INFO] writing run/subrun file: " + path.string());
    return true;
  };

  FrameRecord rec;
  while (queue.Pop(rec)) {
    if (!ofs.is_open()) {
      if (!open_run_file(subrun_number)) {
        return false;
      }
    }

    if (events_in_current_file >= cfg.events_per_file) {
      std::string mysql_error;
      if (!write_run_log(subrun_number, events_in_current_file, subrun_start_time, "completed", mysql_error)) {
        log_line("[ERROR] failed to insert run log into MySQL: " + mysql_error);
        return false;
      }
      ofs.close();
      ++subrun_number;
      if (!open_run_file(subrun_number)) {
        return false;
      }
    }

    if (!rec.payload.empty()) {
      rec.run_number = run_number;
      rec.subrun_number = subrun_number;
      rec.event_number = next_event_number++;

      if (on_frame_ready) {
        on_frame_ready(rec);
      }

      ofs.write(reinterpret_cast<const char*>(rec.payload.data()), static_cast<std::streamsize>(rec.payload.size()));
      if (!ofs.good()) {
        std::string mysql_error;
        if (!write_run_log(subrun_number, events_in_current_file, subrun_start_time, "error", mysql_error)) {
          log_line("[ERROR] failed to insert run log into MySQL: " + mysql_error);
        }
        log_line("Write failed while handling source: " + rec.source);
        return false;
      }

      // Keep file output latency low for online monitoring and crash resilience.
      ofs.flush();
      if (!ofs.good()) {
        std::string mysql_error;
        if (!write_run_log(subrun_number, events_in_current_file, subrun_start_time, "error", mysql_error)) {
          log_line("[ERROR] failed to insert run log into MySQL: " + mysql_error);
        }
        log_line("Flush failed while handling source: " + rec.source);
        return false;
      }
      ++events_in_current_file;
    }
  }

  if (ofs.is_open()) {
    const char* final_status = nullptr;
    if (run_failed.load()) {
      final_status = "error";
    } else if (cfg.events_per_file > 0 && events_in_current_file >= cfg.events_per_file) {
      final_status = "completed";
    } else {
      final_status = "stopped";
    }
    std::string mysql_error;
    if (!write_run_log(subrun_number, events_in_current_file, subrun_start_time, final_status, mysql_error)) {
      log_line("[ERROR] failed to insert run log into MySQL: " + mysql_error);
      return false;
    }
    ofs.close();
  }

  return true;
}

}  // namespace

int RunDaqCore(const DaqConfig& cfg,
               volatile std::sig_atomic_t& stop_requested,
               const FramePublishCallback& on_frame_ready,
               const StartupStatusCallback& on_startup_status,
               const RuntimeErrorCallback& on_runtime_error) {
  DaqConfig effective_cfg = cfg;
  MySqlLogger mysql_logger;
  if (!(effective_cfg.run_start_specified && effective_cfg.allow_existing_run)) {
    std::string run_error;
    uint32_t resolved_run_number = effective_cfg.run_start;
    if (!mysql_logger.ResolveRunNumber(
            effective_cfg.run_start_specified, effective_cfg.run_start, resolved_run_number, run_error)) {
      log_line("[ERROR] failed to resolve run number via MySQL: " + run_error);
      return 1;
    }
    effective_cfg.run_start = resolved_run_number;
    effective_cfg.run_start_specified = true;
  }

  log_line("[INFO] DAQ Run starting");
  BlockingQueue<FrameRecord> queue;
  std::atomic<bool> running{true};
  std::atomic<bool> writer_ok{true};
  std::vector<std::atomic<bool>> connected_once(effective_cfg.devices.size());
  for (size_t i = 0; i < connected_once.size(); ++i) {
    connected_once[i].store(false);
  }
  std::atomic<uint32_t> connected_count{0};
  std::atomic<bool> startup_failed{false};
  std::once_flag startup_notify_once;
  const auto startup_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(effective_cfg.startup_connect_timeout_sec);

  if (effective_cfg.devices.empty()) {
    if (on_startup_status) {
      on_startup_status(false, "startup failed: no devices configured");
    }
    return 1;
  }

  std::thread writer([&]() {
    if (!run_writer(effective_cfg, queue, on_frame_ready, startup_failed)) {
      writer_ok.store(false);
      startup_failed.store(true);
      running.store(false);
      queue.Close();
    }
  });

  std::vector<std::thread> workers;
  workers.reserve(effective_cfg.devices.size());
  for (size_t i = 0; i < effective_cfg.devices.size(); ++i) {
    const DeviceSpec dev = effective_cfg.devices[i];
    workers.emplace_back([&, i, dev]() {
      run_worker(dev,
                 i,
                 effective_cfg,
                 queue,
                 running,
                 stop_requested,
                 startup_deadline,
                 connected_once,
                 connected_count,
                 startup_failed,
                 startup_notify_once,
                 on_startup_status,
                 on_runtime_error);
    });
  }

  const auto start = std::chrono::steady_clock::now();
  while (running.load() && stop_requested == 0) {
    if (effective_cfg.duration_sec > 0) {
      const auto elapsed = std::chrono::steady_clock::now() - start;
      if (elapsed >= std::chrono::seconds(effective_cfg.duration_sec)) {
        log_line("[INFO] duration reached, stopping run");
        running.store(false);
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (stop_requested != 0) {
    if (stop_requested == 2) {
      log_line("[INFO] pause requested");
    } else {
      log_line("[INFO] external stop requested");
    }
  }

  running.store(false);

  for (auto& t : workers) {
    if (t.joinable()) {
      t.join();
    }
  }

  queue.Close();

  if (writer.joinable()) {
    writer.join();
  }

  const int rc = (writer_ok.load() && !startup_failed.load()) ? 0 : 1;
  log_line("[INFO] DAQ Run finished rc=" + std::to_string(rc));
  return rc;
}
