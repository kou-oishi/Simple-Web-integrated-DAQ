#pragma once

#include <cstdint>
#include <string>

class ValidationResult {
 public:
  enum class Status {
    kOk,
    kRecoverableError,
    kFatalError,
  };

  enum class ErrorCode : uint32_t {
    kNone = 0,
    kNullInput = 100,
    kProtocolViolation = 200,
    kHeaderMismatch = 201,
    kChecksumMismatch = 202,
    kInternalError = 900,
    kCustom = 10000,
  };

  static ValidationResult Ok() { return ValidationResult(Status::kOk, ErrorCode::kNone, "", 0, ""); }

  static ValidationResult Recoverable(ErrorCode code,
                                      std::string message,
                                      int64_t detail_code = 0,
                                      std::string detail_tag = "") {
    return ValidationResult(Status::kRecoverableError, code, std::move(message), detail_code, std::move(detail_tag));
  }

  static ValidationResult Fatal(ErrorCode code,
                                std::string message,
                                int64_t detail_code = 0,
                                std::string detail_tag = "") {
    return ValidationResult(Status::kFatalError, code, std::move(message), detail_code, std::move(detail_tag));
  }

  bool IsOk() const { return status_ == Status::kOk; }
  bool IsRecoverable() const { return status_ == Status::kRecoverableError; }
  bool IsFatal() const { return status_ == Status::kFatalError; }

  Status status() const { return status_; }
  ErrorCode code() const { return code_; }
  const std::string& message() const { return message_; }
  int64_t detail_code() const { return detail_code_; }
  const std::string& detail_tag() const { return detail_tag_; }

 private:
  ValidationResult(Status status, ErrorCode code, std::string message, int64_t detail_code, std::string detail_tag)
      : status_(status),
        code_(code),
        message_(std::move(message)),
        detail_code_(detail_code),
        detail_tag_(std::move(detail_tag)) {}

  Status status_;
  ErrorCode code_;
  std::string message_;
  int64_t detail_code_;
  std::string detail_tag_;
};
