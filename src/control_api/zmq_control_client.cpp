#include "control_api/zmq_control_client.hpp"

#include <string>

#include <zmq.h>

namespace {

constexpr int kSendTimeoutMs = 2000;
constexpr int kRecvTimeoutMs = 2000;

}  // namespace

ZmqControlClient::ZmqControlClient(std::string endpoint) : endpoint_(std::move(endpoint)) {}

bool ZmqControlClient::request(const std::string& request, std::string& reply, std::string& error_text) const {
  reply.clear();
  error_text.clear();

  void* ctx = zmq_ctx_new();
  if (ctx == nullptr) {
    error_text = "zmq_ctx_new failed";
    return false;
  }

  void* req_sock = zmq_socket(ctx, ZMQ_REQ);
  if (req_sock == nullptr) {
    error_text = "zmq_socket(ZMQ_REQ) failed";
    zmq_ctx_term(ctx);
    return false;
  }

  zmq_setsockopt(req_sock, ZMQ_SNDTIMEO, &kSendTimeoutMs, sizeof(kSendTimeoutMs));
  zmq_setsockopt(req_sock, ZMQ_RCVTIMEO, &kRecvTimeoutMs, sizeof(kRecvTimeoutMs));

  if (zmq_connect(req_sock, endpoint_.c_str()) != 0) {
    error_text = "zmq_connect failed: " + endpoint_;
    zmq_close(req_sock);
    zmq_ctx_term(ctx);
    return false;
  }

  if (zmq_send(req_sock, request.data(), request.size(), 0) < 0) {
    error_text = "send failed";
    zmq_close(req_sock);
    zmq_ctx_term(ctx);
    return false;
  }

  zmq_msg_t msg;
  zmq_msg_init(&msg);
  if (zmq_msg_recv(&msg, req_sock, 0) < 0) {
    error_text = "recv failed";
    zmq_msg_close(&msg);
    zmq_close(req_sock);
    zmq_ctx_term(ctx);
    return false;
  }

  const char* data = static_cast<const char*>(zmq_msg_data(&msg));
  const size_t size = zmq_msg_size(&msg);
  reply.assign(data, data + size);
  zmq_msg_close(&msg);

  zmq_close(req_sock);
  zmq_ctx_term(ctx);
  return true;
}
