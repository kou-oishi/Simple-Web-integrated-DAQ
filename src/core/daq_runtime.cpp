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
#include "core/device_frontend_registry.hpp"
#include "core/validation_result.hpp"

namespace {

const char* to_string(ValidationResult::Status s) {
  switch (s) {
    case ValidationResult::Status::kOk:
      return "ok";
    case ValidationResult::Status::kRecoverableError:
      return "recoverable";
    case ValidationResult::Status::kFatalError:
      return "fatal";
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
    queue.close();
    return;
  }

  std::unique_ptr<IDeviceDriver> driver = frontend->create_driver(spec);
  std::unique_ptr<IDataValidator> validator = frontend->create_validator(spec);
  if (!driver || !validator) {
    std::cerr << "[ERROR] failed to create driver/validator for frontend: " << spec.frontend << "\n";
    running.store(false);
    queue.close();
    return;
  }

  while (running.load() && stop_requested == 0) {
    if (!driver->connect_device()) {
      std::cerr << "[WARN] connect failed: " << device_label(spec) << "\n";
      std::this_thread::sleep_for(std::chrono::milliseconds(cfg.reconnect_ms));
      continue;
    }

    std::cerr << "[INFO] connected: " << device_label(spec) << "\n";

    while (running.load() && stop_requested == 0) {
      std::vector<uint8_t> chunk;
      const ReadStatus st = driver->read_bytes(
          chunk, daq_defaults::kReadChunkSizeBytes, static_cast<int>(cfg.read_timeout_ms));
      if (st == ReadStatus::kTimeout) {
        continue;
      }
      if (st != ReadStatus::kOk) {
        std::cerr << "[WARN] disconnected/read-error: " << device_label(spec) << "\n";
        driver->disconnect_device();
        validator->reset();
        break;
      }

      std::vector<std::vector<uint8_t>> frames;
      const ValidationResult vr = validator->feed(chunk.data(), chunk.size(), frames);
      if (!vr.ok()) {
        std::cerr << "[ERROR] validator error on " << device_label(spec)
                  << " status=" << to_string(vr.status())
                  << " code=" << static_cast<uint32_t>(vr.code())
                  << " detail_code=" << vr.detail_code()
                  << " detail_tag=" << vr.detail_tag()
                  << " message=" << vr.message() << "\n";

        if (vr.fatal()) {
          running.store(false);
          queue.close();
          return;
        }

        driver->disconnect_device();
        validator->reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.reconnect_ms));
        break;
      }

      for (auto& frame : frames) {
        FrameRecord rec;
        rec.source = "board" + std::to_string(static_cast<unsigned>(spec.board_id));
        rec.payload = std::move(frame);
        if (!queue.push(std::move(rec))) {
          return;
        }
      }
    }
  }

  driver->disconnect_device();
}

bool run_writer(const DaqConfig& cfg, BlockingQueue<FrameRecord>& queue, const FramePublishCallback& on_frame_ready) {
  std::error_code ec;
  std::filesystem::create_directories(cfg.output_dir, ec);
  if (ec) {
    std::cerr << "Failed to create output_dir: " << cfg.output_dir << " (" << ec.message() << ")\n";
    return false;
  }

  auto make_run_path = [&](uint32_t run_number) -> std::filesystem::path {
    std::ostringstream oss;
    oss << "run" << std::setw(daq_defaults::kRunNumberWidth) << std::setfill('0') << run_number << ".dat";
    return std::filesystem::path(cfg.output_dir) / oss.str();
  };

  uint32_t run_number = cfg.run_start;
  uint32_t events_in_current_file = 0;
  std::ofstream ofs;

  auto open_run_file = [&](uint32_t run) -> bool {
    const std::filesystem::path path = make_run_path(run);
    ofs = std::ofstream(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
      std::cerr << "Failed to open output file: " << path.string() << "\n";
      return false;
    }
    events_in_current_file = 0;
    std::cerr << "[INFO] writing run file: " << path.string() << "\n";
    return true;
  };

  FrameRecord rec;
  while (queue.pop(rec)) {
    if (!ofs.is_open()) {
      if (!open_run_file(run_number)) {
        return false;
      }
    }

    if (events_in_current_file >= cfg.events_per_file) {
      ofs.close();
      ++run_number;
      if (!open_run_file(run_number)) {
        return false;
      }
    }

    if (!rec.payload.empty()) {
      if (on_frame_ready) {
        on_frame_ready(rec);
      }

      ofs.write(reinterpret_cast<const char*>(rec.payload.data()), static_cast<std::streamsize>(rec.payload.size()));
      if (!ofs.good()) {
        std::cerr << "Write failed while handling source: " << rec.source << "\n";
        return false;
      }

      // Keep file output latency low for online monitoring and crash resilience.
      ofs.flush();
      if (!ofs.good()) {
        std::cerr << "Flush failed while handling source: " << rec.source << "\n";
        return false;
      }
      ++events_in_current_file;
    }
  }

  return true;
}

}  // namespace

int RunDaqCore(const DaqConfig& cfg,
               volatile std::sig_atomic_t& stop_requested,
               const FramePublishCallback& on_frame_ready) {
  std::cerr << "[INFO] DAQ run starting\n";
  BlockingQueue<FrameRecord> queue;
  std::atomic<bool> running{true};
  std::atomic<bool> writer_ok{true};

  std::thread writer([&]() {
    if (!run_writer(cfg, queue, on_frame_ready)) {
      writer_ok.store(false);
      running.store(false);
      queue.close();
    }
  });

  std::vector<std::thread> workers;
  workers.reserve(cfg.devices.size());
  for (const auto& dev : cfg.devices) {
    workers.emplace_back([&, dev]() { run_worker(dev, cfg, queue, running, stop_requested); });
  }

  const auto start = std::chrono::steady_clock::now();
  while (running.load() && stop_requested == 0) {
    if (cfg.duration_sec > 0) {
      const auto elapsed = std::chrono::steady_clock::now() - start;
      if (elapsed >= std::chrono::seconds(cfg.duration_sec)) {
        std::cerr << "[INFO] duration reached, stopping run\n";
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

  queue.close();

  if (writer.joinable()) {
    writer.join();
  }

  const int rc = writer_ok.load() ? 0 : 1;
  std::cerr << "[INFO] DAQ run finished rc=" << rc << "\n";
  return rc;
}
