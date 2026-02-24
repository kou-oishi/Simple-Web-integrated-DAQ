#pragma once

#include <chrono>
#include <cstdint>
#include <string>

struct RunLogEntry {
  uint32_t run_number = 0;
  uint32_t subrun_number = 0;
  uint64_t event_count = 0;
  std::chrono::system_clock::time_point start_time;
  std::chrono::system_clock::time_point end_time;
  std::string status;
  std::string connected_modules;
  std::string disconnected_modules;
  std::string comment;
};

class MySqlLogger {
 public:
  bool IsEnabled() const;
  bool InsertRunLog(const RunLogEntry& entry, std::string& error_text) const;
  bool UpdateRunLogStatus(uint32_t run_number, uint32_t subrun_number, const std::string& status, std::string& error_text) const;
  bool UpdateRunLogModules(uint32_t run_number,
                           const std::string& connected_modules,
                           const std::string& disconnected_modules,
                           std::string& error_text) const;
  bool ResolveRunNumber(bool run_number_specified,
                        uint32_t requested_run_number,
                        uint32_t& out_run_number,
                        std::string& error_text) const;

 private:
  bool GetLastRunNumber(uint32_t& out_last_run_number, bool& out_has_rows, std::string& error_text) const;
  bool RunNumberExists(uint32_t run_number, bool& out_exists, std::string& error_text) const;
  bool QueryFirstCell(const std::string& query, std::string& out_cell, std::string& error_text) const;
  static std::string EscapeSql(const std::string& text);
  static std::string ShellQuote(const std::string& text);
  static std::string FormatTime(const std::chrono::system_clock::time_point& time_point);
  bool ExecuteQuery(const std::string& query, std::string& error_text) const;
};
