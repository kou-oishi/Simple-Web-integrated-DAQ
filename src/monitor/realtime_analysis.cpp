#include "monitor/realtime_analysis.hpp"

#include <cctype>
#include <limits>
#include <unordered_set>

namespace {

std::string trim_copy(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }

  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }

  return text.substr(begin, end - begin);
}

}  // namespace

bool IRealtimeAnalysis::RegisterCanvas(TCanvas* canvas, std::string& error_text) const {
  error_text.clear();
  if (display_registry_ == nullptr) {
    error_text = "realtime analysis display registry is not set";
    return false;
  }
  return display_registry_->RegisterCanvas(canvas, error_text);
}

bool IRealtimeAnalysis::RegisterDrawable(TVirtualPad* pad,
                                          TObject* object,
                                          const char* draw_option,
                                          std::string& error_text) const {
  error_text.clear();
  if (display_registry_ == nullptr) {
    error_text = "realtime analysis display registry is not set";
    return false;
  }
  return display_registry_->RegisterDrawable(pad, object, draw_option, error_text);
}

bool ParseUnsignedU64(const std::string& text, uint64_t& out) {
  try {
    std::size_t pos = 0;
    const unsigned long long value = std::stoull(text, &pos, 10);
    if (pos != text.size()) {
      return false;
    }
    out = static_cast<uint64_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseUnsignedSize(const std::string& text, std::size_t& out) {
  uint64_t value = 0;
  if (!ParseUnsignedU64(text, value) || value > std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  out = static_cast<std::size_t>(value);
  return true;
}

bool ParseKeyValueSpec(const std::string& spec,
                       std::vector<std::pair<std::string, std::string>>& out_pairs,
                       std::string& error_text) {
  out_pairs.clear();
  error_text.clear();
  if (spec.empty()) {
    return true;
  }

  std::size_t begin = 0;
  while (begin <= spec.size()) {
    const std::size_t end = spec.find(',', begin);
    const std::string token = trim_copy(spec.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
    if (!token.empty()) {
      const std::size_t eq = token.find('=');
      if (eq == std::string::npos || eq == 0 || eq + 1 >= token.size()) {
        error_text = "invalid spec token: '" + token + "'";
        return false;
      }

      const std::string key = trim_copy(token.substr(0, eq));
      const std::string value = trim_copy(token.substr(eq + 1));
      if (key.empty() || value.empty()) {
        error_text = "invalid empty key/value in spec token: '" + token + "'";
        return false;
      }
      out_pairs.emplace_back(key, value);
    }

    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }

  return true;
}

bool ParsedAnalysisSpec::Parse(const std::string& spec, std::string& error_text) {
  return ParseKeyValueSpec(spec, kv_pairs_, error_text);
}

bool ParsedAnalysisSpec::ReadU64(const char* key,
                                  uint64_t default_value,
                                  uint64_t& out,
                                  std::string& error_text) const {
  out = default_value;
  error_text.clear();
  for (const auto& kv : kv_pairs_) {
    if (kv.first == key) {
      if (!ParseUnsignedU64(kv.second, out)) {
        error_text = std::string("analysis spec '") + key + "' must be unsigned integer";
        return false;
      }
    }
  }
  return true;
}

bool ParsedAnalysisSpec::ReadSize(const char* key,
                                   std::size_t default_value,
                                   std::size_t& out,
                                   std::string& error_text) const {
  out = default_value;
  error_text.clear();
  for (const auto& kv : kv_pairs_) {
    if (kv.first == key) {
      if (!ParseUnsignedSize(kv.second, out)) {
        error_text = std::string("analysis spec '") + key + "' must be unsigned integer";
        return false;
      }
    }
  }
  return true;
}

bool ParsedAnalysisSpec::RejectUnknown(std::initializer_list<const char*> allowed_keys, std::string& error_text) const {
  error_text.clear();
  std::unordered_set<std::string> allowed;
  for (const char* key : allowed_keys) {
    if (key != nullptr) {
      allowed.insert(key);
    }
  }

  for (const auto& kv : kv_pairs_) {
    if (allowed.find(kv.first) == allowed.end()) {
      error_text = "unknown analysis spec key: '" + kv.first + "'";
      return false;
    }
  }
  return true;
}
