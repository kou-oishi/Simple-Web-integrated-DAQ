#include "core/daq_runtime.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core/blocking_queue.hpp"
#include "core/defaults.hpp"
#include "concrete_modules/module_registry.hpp"
#include "core/mysql_logger.hpp"
#include "core/validation_result.hpp"

namespace {

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
                const DaqConfig& cfg,
                BlockingQueue<FrameRecord>& queue,
                std::atomic<bool>& running,
                volatile std::sig_atomic_t& stop_requested) {
  const IDeviceFrontend* frontend = FindDeviceFrontend(spec.frontend);
  if (frontend == nullptr) {
    std::cerr << "[ERROR] unknown frontend in runtime: " << spec.frontend << "\n";
    running.store(false);
    queue.Close();
    return;
  }

  std::unique_ptr<IDeviceDriver> driver = frontend->CreateDriver(spec);
  std::unique_ptr<IDataValidator> validator = frontend->CreateValidator(spec);
  if (!driver || !validator) {
    std::cerr << "[ERROR] failed to Create driver/validator for frontend: " << spec.frontend << "\n";
    running.store(false);
    queue.Close();
    return;
  }

  while (running.load() && stop_requested == 0) {
    if (!driver->ConnectDevice()) {
      std::cerr << "[WARN] Connect failed: " << device_label(spec) << "\n";
      std::this_thread::sleep_for(std::chrono::milliseconds(cfg.reconnect_ms));
      continue;
    }

    std::cerr << "[INFO] connected: " << device_label(spec) << "\n";

    while (running.load() && stop_requested == 0) {
      std::vector<uint8_t> chunk;
      const ReadStatus st = driver->ReadBytes(
          chunk, daq_defaults::kReadChunkSizeBytes, static_cast<int>(cfg.read_timeout_ms));
      if (st == ReadStatus::kTimeout) {
        continue;
      }
      if (st != ReadStatus::kOk) {
        std::cerr << "[WARN] disconnected/read-error: " << device_label(spec) << "\n";
        driver->DisconnectDevice();
        validator->Reset();
        break;
      }

      std::vector<std::vector<uint8_t>> frames;
      const ValidationResult vr = validator->Feed(chunk.data(), chunk.size(), frames);
      if (!vr.IsOk()) {
        std::cerr << "[ERROR] validator error on " << device_label(spec)
                  << " status=" << to_string(vr.status())
                  << " code=" << static_cast<uint32_t>(vr.code())
                  << " detail_code=" << vr.detail_code()
                  << " detail_tag=" << vr.detail_tag()
                  << " message=" << vr.message() << "\n";

        if (vr.IsFatal()) {
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

bool run_writer(const DaqConfig& cfg, BlockingQueue<FrameRecord>& queue, const FramePublishCallback& on_frame_ready) {
  std::error_code ec;
  std::filesystem::create_directories(cfg.output_dir, ec);
  if (ec) {
    std::cerr << "Failed to Create output_dir: " << cfg.output_dir << " (" << ec.message() << ")\n";
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
  uint32_t subrun_number = 0;
  uint32_t events_in_current_file = 0;
  uint64_t next_event_number = 0;
  std::chrono::system_clock::time_point subrun_start_time{};
  std::ofstream ofs;
  MySqlLogger mysql_logger;

  auto write_subrun_log = [&](uint32_t subrun,
                              uint64_t event_count,
                              const std::chrono::system_clock::time_point& start_time,
                              const char* status,
                              std::string& out_error_text) -> bool {
    out_error_text.clear();
    if (event_count == 0 || !mysql_logger.IsEnabled()) {
      return true;
    }
    SubrunLogEntry entry;
    entry.run_number = run_number;
    entry.subrun_number = subrun;
    entry.event_count = event_count;
    entry.start_time = start_time;
    entry.end_time = std::chrono::system_clock::now();
    entry.status = status == nullptr ? "stopped" : status;
    entry.comment = cfg.comment;
    return mysql_logger.InsertSubrun(entry, out_error_text);
  };

  auto open_run_file = [&](uint32_t subrun) -> bool {
    const std::filesystem::path path = make_run_path(run_number, subrun);
    ofs = std::ofstream(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
      std::cerr << "Failed to open output file: " << path.string() << "\n";
      return false;
    }
    events_in_current_file = 0;
    subrun_start_time = std::chrono::system_clock::now();
    std::cerr << "[INFO] writing run/subrun file: " << path.string() << "\n";
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
      if (!write_subrun_log(subrun_number, events_in_current_file, subrun_start_time, "completed", mysql_error)) {
        std::cerr << "[ERROR] failed to insert subrun log into MySQL: " << mysql_error << "\n";
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
        if (!write_subrun_log(subrun_number, events_in_current_file, subrun_start_time, "error", mysql_error)) {
          std::cerr << "[ERROR] failed to insert subrun log into MySQL: " << mysql_error << "\n";
        }
        std::cerr << "Write failed while handling source: " << rec.source << "\n";
        return false;
      }

      // Keep file output latency low for online monitoring and crash resilience.
      ofs.flush();
      if (!ofs.good()) {
        std::string mysql_error;
        if (!write_subrun_log(subrun_number, events_in_current_file, subrun_start_time, "error", mysql_error)) {
          std::cerr << "[ERROR] failed to insert subrun log into MySQL: " << mysql_error << "\n";
        }
        std::cerr << "Flush failed while handling source: " << rec.source << "\n";
        return false;
      }
      ++events_in_current_file;
    }
  }

  if (ofs.is_open()) {
    const char* final_status =
        (cfg.events_per_file > 0 && events_in_current_file >= cfg.events_per_file) ? "completed" : "stopped";
    std::string mysql_error;
    if (!write_subrun_log(subrun_number, events_in_current_file, subrun_start_time, final_status, mysql_error)) {
      std::cerr << "[ERROR] failed to insert subrun log into MySQL: " << mysql_error << "\n";
      return false;
    }
    ofs.close();
  }

  return true;
}

}  // namespace

int RunDaqCore(const DaqConfig& cfg,
               volatile std::sig_atomic_t& stop_requested,
               const FramePublishCallback& on_frame_ready) {
  DaqConfig effective_cfg = cfg;
  MySqlLogger mysql_logger;
  std::string run_error;
  uint32_t resolved_run_number = effective_cfg.run_start;
  if (!mysql_logger.ResolveRunNumber(
          effective_cfg.run_start_specified, effective_cfg.run_start, resolved_run_number, run_error)) {
    std::cerr << "[ERROR] failed to resolve run number via MySQL: " << run_error << "\n";
    return 1;
  }
  effective_cfg.run_start = resolved_run_number;
  effective_cfg.run_start_specified = true;

  std::cerr << "[INFO] DAQ Run starting\n";
  BlockingQueue<FrameRecord> queue;
  std::atomic<bool> running{true};
  std::atomic<bool> writer_ok{true};

  std::thread writer([&]() {
    if (!run_writer(effective_cfg, queue, on_frame_ready)) {
      writer_ok.store(false);
      running.store(false);
      queue.Close();
    }
  });

  std::vector<std::thread> workers;
  workers.reserve(effective_cfg.devices.size());
  for (const auto& dev : effective_cfg.devices) {
    workers.emplace_back([&, dev]() { run_worker(dev, effective_cfg, queue, running, stop_requested); });
  }

  const auto start = std::chrono::steady_clock::now();
  while (running.load() && stop_requested == 0) {
    if (effective_cfg.duration_sec > 0) {
      const auto elapsed = std::chrono::steady_clock::now() - start;
      if (elapsed >= std::chrono::seconds(effective_cfg.duration_sec)) {
        std::cerr << "[INFO] duration reached, stopping Run\n";
        running.store(false);
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (stop_requested != 0) {
    std::cerr << "[INFO] external stop requested\n";
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

  const int rc = writer_ok.load() ? 0 : 1;
  std::cerr << "[INFO] DAQ Run finished rc=" << rc << "\n";
  return rc;
}
