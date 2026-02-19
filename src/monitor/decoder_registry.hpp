#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "monitor/decoder.hpp"

class IMonitorDecoderFactory {
 public:
  virtual ~IMonitorDecoderFactory() = default;

  virtual const char* name() const = 0;
  virtual bool create(const std::string& spec,
                      std::unique_ptr<IDecoder>& out_decoder,
                      std::size_t& out_frame_size,
                      std::string& error_text) const = 0;
};

const IMonitorDecoderFactory* FindMonitorDecoderFactory(const std::string& name);
std::vector<std::string> ListMonitorDecoderFactories();
