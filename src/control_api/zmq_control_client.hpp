#pragma once

#include <string>

class ZmqControlClient {
 public:
  explicit ZmqControlClient(std::string endpoint);

  // Returns true when a reply was received. The reply text is always written.
  bool Request(const std::string& Request, std::string& reply, std::string& error_text) const;

 private:
  std::string endpoint_;
};
