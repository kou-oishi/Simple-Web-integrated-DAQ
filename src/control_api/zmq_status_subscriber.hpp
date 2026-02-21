#pragma once

#include <string>

class ZmqStatusSubscriber {
 public:
  explicit ZmqStatusSubscriber(std::string endpoint);
  ~ZmqStatusSubscriber();

  bool Connect(std::string& error_text);
  bool ReceiveNext(std::string& payload, std::string& error_text);

 private:
  std::string endpoint_;
  void* ctx_ = nullptr;
  void* sub_ = nullptr;
};
