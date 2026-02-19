#include "monitor/zmq_frame_source.hpp"

#include <chrono>
#include <cstring>

#include <zmq.h>

namespace {

bool recv_msg(void* socket, zmq_msg_t* msg) {
  while (true) {
    const int rc = zmq_msg_recv(msg, socket, 0);
    if (rc >= 0) {
      return true;
    }
    if (zmq_errno() == EINTR) {
      continue;
    }
    return false;
  }
}

}  // namespace

ZmqDataFrameSource::ZmqDataFrameSource(std::string endpoint, uint32_t poll_timeout_ms, uint32_t idle_timeout_sec)
    : endpoint_(std::move(endpoint)),
      poll_timeout_ms_(poll_timeout_ms == 0 ? 200 : poll_timeout_ms),
      idle_timeout_sec_(idle_timeout_sec) {}

ZmqDataFrameSource::~ZmqDataFrameSource() {
  if (sub_ != nullptr) {
    zmq_close(sub_);
    sub_ = nullptr;
  }
  if (ctx_ != nullptr) {
    zmq_ctx_term(ctx_);
    ctx_ = nullptr;
  }
}

bool ZmqDataFrameSource::ensure_connected(std::string& error_text) {
  if (connected_) {
    return true;
  }

  ctx_ = zmq_ctx_new();
  if (ctx_ == nullptr) {
    error_text = "zmq_ctx_new failed";
    return false;
  }

  sub_ = zmq_socket(ctx_, ZMQ_SUB);
  if (sub_ == nullptr) {
    error_text = "zmq_socket(ZMQ_SUB) failed";
    zmq_ctx_term(ctx_);
    ctx_ = nullptr;
    return false;
  }

  const int rcv_timeout = static_cast<int>(poll_timeout_ms_);
  zmq_setsockopt(sub_, ZMQ_RCVTIMEO, &rcv_timeout, sizeof(rcv_timeout));

  const char* topic = "data";
  zmq_setsockopt(sub_, ZMQ_SUBSCRIBE, topic, std::strlen(topic));

  if (zmq_connect(sub_, endpoint_.c_str()) != 0) {
    error_text = "zmq_connect failed: " + endpoint_;
    zmq_close(sub_);
    zmq_ctx_term(ctx_);
    sub_ = nullptr;
    ctx_ = nullptr;
    return false;
  }

  connected_ = true;
  return true;
}

SourceStatus ZmqDataFrameSource::next_frame(std::vector<uint8_t>& out_frame, std::string& error_text) {
  out_frame.clear();
  error_text.clear();

  if (!ensure_connected(error_text)) {
    return SourceStatus::kError;
  }

  const auto wait_begin = std::chrono::steady_clock::now();
  auto timed_out = [&]() {
    if (idle_timeout_sec_ == 0) {
      return false;
    }
    return (std::chrono::steady_clock::now() - wait_begin) >= std::chrono::seconds(idle_timeout_sec_);
  };

  while (true) {
    zmq_msg_t topic_msg;
    zmq_msg_t source_msg;
    zmq_msg_t payload_msg;
    zmq_msg_init(&topic_msg);
    zmq_msg_init(&source_msg);
    zmq_msg_init(&payload_msg);

    const bool ok_topic = recv_msg(sub_, &topic_msg);
    if (!ok_topic) {
      const int e = zmq_errno();
      zmq_msg_close(&topic_msg);
      zmq_msg_close(&source_msg);
      zmq_msg_close(&payload_msg);
      if (e == EAGAIN) {
        if (timed_out()) {
          return SourceStatus::kEof;
        }
        continue;
      }
      error_text = "recv data topic failed";
      return SourceStatus::kError;
    }

    const bool ok_source = recv_msg(sub_, &source_msg);
    const bool ok_payload = ok_source ? recv_msg(sub_, &payload_msg) : false;

    if (!ok_source || !ok_payload) {
      zmq_msg_close(&topic_msg);
      zmq_msg_close(&source_msg);
      zmq_msg_close(&payload_msg);
      error_text = "recv multipart data frame failed";
      return SourceStatus::kError;
    }

    const char* topic_data = static_cast<const char*>(zmq_msg_data(&topic_msg));
    const size_t topic_size = zmq_msg_size(&topic_msg);
    const std::string topic(topic_data, topic_data + topic_size);

    if (topic != "data") {
      zmq_msg_close(&topic_msg);
      zmq_msg_close(&source_msg);
      zmq_msg_close(&payload_msg);
      continue;
    }

    const auto* payload_data = static_cast<const uint8_t*>(zmq_msg_data(&payload_msg));
    const size_t payload_size = zmq_msg_size(&payload_msg);
    out_frame.assign(payload_data, payload_data + payload_size);

    zmq_msg_close(&topic_msg);
    zmq_msg_close(&source_msg);
    zmq_msg_close(&payload_msg);

    if (out_frame.empty()) {
      error_text = "received empty payload frame";
      return SourceStatus::kError;
    }

    return SourceStatus::kOk;
  }
}
