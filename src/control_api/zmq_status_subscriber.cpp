#include "control_api/zmq_status_subscriber.hpp"

#include <cstring>

#include <zmq.h>

ZmqStatusSubscriber::ZmqStatusSubscriber(std::string endpoint) : endpoint_(std::move(endpoint)) {}

ZmqStatusSubscriber::~ZmqStatusSubscriber() {
  if (sub_ != nullptr) {
    zmq_close(sub_);
    sub_ = nullptr;
  }
  if (ctx_ != nullptr) {
    zmq_ctx_term(ctx_);
    ctx_ = nullptr;
  }
}

bool ZmqStatusSubscriber::Connect(std::string& error_text) {
  error_text.clear();

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

  const char* topic = "status";
  zmq_setsockopt(sub_, ZMQ_SUBSCRIBE, topic, std::strlen(topic));

  if (zmq_connect(sub_, endpoint_.c_str()) != 0) {
    error_text = "zmq_connect failed: " + endpoint_;
    zmq_close(sub_);
    zmq_ctx_term(ctx_);
    sub_ = nullptr;
    ctx_ = nullptr;
    return false;
  }

  return true;
}

bool ZmqStatusSubscriber::ReceiveNext(std::string& payload, std::string& error_text) {
  payload.clear();
  error_text.clear();

  if (sub_ == nullptr) {
    error_text = "subscriber not connected";
    return false;
  }

  zmq_msg_t tmsg;
  zmq_msg_t pmsg;
  zmq_msg_init(&tmsg);
  zmq_msg_init(&pmsg);

  if (zmq_msg_recv(&tmsg, sub_, 0) < 0) {
    error_text = "recv topic failed";
    zmq_msg_close(&tmsg);
    zmq_msg_close(&pmsg);
    return false;
  }
  if (zmq_msg_recv(&pmsg, sub_, 0) < 0) {
    error_text = "recv payload failed";
    zmq_msg_close(&tmsg);
    zmq_msg_close(&pmsg);
    return false;
  }

  const char* data = static_cast<const char*>(zmq_msg_data(&pmsg));
  const size_t size = zmq_msg_size(&pmsg);
  payload.assign(data, data + size);

  zmq_msg_close(&tmsg);
  zmq_msg_close(&pmsg);
  return true;
}
