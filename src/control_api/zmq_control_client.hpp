#pragma once

#include <string>

class ZmqControlClient {
 public:
  explicit ZmqControlClient(std::string endpoint);

  // Returns true when a reply was received. The reply text is always written.
  bool request(const std::string& request, std::string& reply, std::string& error_text) const;

 private:
  std::string endpoint_;
};
