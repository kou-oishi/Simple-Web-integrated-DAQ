#include "core/mysql_logger.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>

#include <sys/wait.h>

#include "core/defaults.hpp"

namespace {

std::string quote_status(const std::string& status) {
  if (status.empty()) {
    return "stopped";
  }
  return status;
}

}  // namespace

bool MySqlLogger::IsEnabled() const { return daq_defaults::kMySqlEnabled; }

bool MySqlLogger::InsertRunLog(const RunLogEntry& entry, std::string& error_text) const {
  if (!IsEnabled()) {
    return true;
  }

  std::ostringstream sql;
  sql << "INSERT INTO `" << daq_defaults::kMySqlRunLogTable << "` "
      << "(run, subrun, nevents, start_time, end_time, status, comment) VALUES ("
      << static_cast<unsigned long long>(entry.run_number) << ", "
      << static_cast<unsigned long long>(entry.subrun_number) << ", "
      << static_cast<unsigned long long>(entry.event_count) << ", "
      << "'" << EscapeSql(FormatTime(entry.start_time)) << "', "
      << "'" << EscapeSql(FormatTime(entry.end_time)) << "', "
      << "'" << EscapeSql(quote_status(entry.status)) << "', "
      << "'" << EscapeSql(entry.comment) << "')";

  return ExecuteQuery(sql.str(), error_text);
}

bool MySqlLogger::UpdateRunLogStatus(uint32_t run_number,
                                     uint32_t subrun_number,
                                     const std::string& status,
                                     std::string& error_text) const {
  if (!IsEnabled()) {
    error_text.clear();
    return true;
  }

  std::ostringstream sql;
  sql << "UPDATE `" << daq_defaults::kMySqlRunLogTable << "` "
      << "SET status='" << EscapeSql(quote_status(status)) << "' "
      << "WHERE run=" << static_cast<unsigned long long>(run_number)
      << " AND subrun=" << static_cast<unsigned long long>(subrun_number);
  return ExecuteQuery(sql.str(), error_text);
}

bool MySqlLogger::ResolveRunNumber(bool run_number_specified,
                                   uint32_t requested_run_number,
                                   uint32_t& out_run_number,
                                   std::string& error_text) const {
  if (!IsEnabled()) {
    out_run_number = requested_run_number;
    error_text.clear();
    return true;
  }

  if (run_number_specified) {
    bool exists = false;
    if (!RunNumberExists(requested_run_number, exists, error_text)) {
      return false;
    }
    if (exists) {
      error_text = "requested run number already exists in DB: " + std::to_string(requested_run_number);
      return false;
    }
    out_run_number = requested_run_number;
    error_text.clear();
    return true;
  }

  uint32_t last_run_number = 0;
  bool has_rows = false;
  if (!GetLastRunNumber(last_run_number, has_rows, error_text)) {
    return false;
  }

  out_run_number = has_rows ? (last_run_number + 1) : daq_defaults::kRunStart;
  error_text.clear();
  return true;
}

bool MySqlLogger::GetLastRunNumber(uint32_t& out_last_run_number, bool& out_has_rows, std::string& error_text) const {
  std::string cell;
  if (!QueryFirstCell("SELECT MAX(run) FROM `" + std::string(daq_defaults::kMySqlRunLogTable) + "`",
                      cell,
                      error_text)) {
    return false;
  }

  if (cell.empty() || cell == "NULL") {
    out_last_run_number = 0;
    out_has_rows = false;
    error_text.clear();
    return true;
  }

  try {
    std::size_t pos = 0;
    const unsigned long long value = std::stoull(cell, &pos, 10);
    if (pos != cell.size()) {
      error_text = "invalid MAX(run) response from DB: " + cell;
      return false;
    }
    out_last_run_number = static_cast<uint32_t>(value);
    out_has_rows = true;
    error_text.clear();
    return true;
  } catch (...) {
    error_text = "failed to parse MAX(run) response from DB: " + cell;
    return false;
  }
}

bool MySqlLogger::RunNumberExists(uint32_t run_number, bool& out_exists, std::string& error_text) const {
  std::string cell;
  if (!QueryFirstCell("SELECT 1 FROM `" + std::string(daq_defaults::kMySqlRunLogTable) +
                          "` WHERE run=" + std::to_string(run_number) + " LIMIT 1",
                      cell,
                      error_text)) {
    return false;
  }
  out_exists = !cell.empty();
  error_text.clear();
  return true;
}

bool MySqlLogger::QueryFirstCell(const std::string& query, std::string& out_cell, std::string& error_text) const {
  out_cell.clear();
  error_text.clear();

  std::ostringstream cmd;
  cmd << "MYSQL_PWD=" << ShellQuote(daq_defaults::kMySqlPassword)
      << " mysql --protocol=TCP"
      << " --host=" << ShellQuote(daq_defaults::kMySqlHost)
      << " --port=" << daq_defaults::kMySqlPort
      << " --user=" << ShellQuote(daq_defaults::kMySqlUser)
      << " --database=" << ShellQuote(daq_defaults::kMySqlDatabase)
      << " --batch --skip-column-names --raw"
      << " --execute=" << ShellQuote(query);

  FILE* pipe = ::popen(cmd.str().c_str(), "r");
  if (pipe == nullptr) {
    error_text = "failed to execute mysql client";
    return false;
  }

  char buffer[256];
  std::string output;
  while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr) {
    output += buffer;
  }

  const int rc = ::pclose(pipe);
  if (rc != 0) {
    if (WIFEXITED(rc)) {
      error_text = "mysql client exited with code " + std::to_string(WEXITSTATUS(rc));
    } else {
      error_text = "mysql client exited abnormally";
    }
    return false;
  }

  std::size_t end = output.find('\n');
  if (end != std::string::npos) {
    output.resize(end);
  }
  while (!output.empty() && std::isspace(static_cast<unsigned char>(output.back())) != 0) {
    output.pop_back();
  }
  std::size_t begin = 0;
  while (begin < output.size() && std::isspace(static_cast<unsigned char>(output[begin])) != 0) {
    ++begin;
  }
  out_cell = output.substr(begin);
  return true;
}

std::string MySqlLogger::EscapeSql(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (const char c : text) {
    if (c == '\\' || c == '\'') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

std::string MySqlLogger::ShellQuote(const std::string& text) {
  std::string out = "'";
  for (const char c : text) {
    if (c == '\'') {
      out += "'\\''";
      continue;
    }
    out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

std::string MySqlLogger::FormatTime(const std::chrono::system_clock::time_point& time_point) {
  const std::time_t raw_time = std::chrono::system_clock::to_time_t(time_point);
  std::tm tm_value{};
#if defined(_WIN32)
  localtime_s(&tm_value, &raw_time);
#else
  localtime_r(&raw_time, &tm_value);
#endif

  std::ostringstream oss;
  oss << std::put_time(&tm_value, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

bool MySqlLogger::ExecuteQuery(const std::string& query, std::string& error_text) const {
  error_text.clear();
  std::ostringstream cmd;
  cmd << "MYSQL_PWD=" << ShellQuote(daq_defaults::kMySqlPassword)
      << " mysql --protocol=TCP"
      << " --host=" << ShellQuote(daq_defaults::kMySqlHost)
      << " --port=" << daq_defaults::kMySqlPort
      << " --user=" << ShellQuote(daq_defaults::kMySqlUser)
      << " --database=" << ShellQuote(daq_defaults::kMySqlDatabase);

  FILE* pipe = ::popen(cmd.str().c_str(), "w");
  if (pipe == nullptr) {
    error_text = "failed to execute mysql client";
    return false;
  }

  const std::string statement = query + ";\n";
  const std::size_t written = std::fwrite(statement.data(), 1, statement.size(), pipe);
  if (written != statement.size()) {
    (void)::pclose(pipe);
    error_text = "failed to write SQL statement to mysql client";
    return false;
  }

  const int rc = ::pclose(pipe);
  if (rc != 0) {
    if (WIFEXITED(rc)) {
      error_text = "mysql client exited with code " + std::to_string(WEXITSTATUS(rc));
    } else {
      error_text = "mysql client exited abnormally";
    }
    return false;
  }
  return true;
}
