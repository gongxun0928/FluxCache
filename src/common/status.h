#pragma once

#include <cstdint>
#include <string>

namespace fluxcache {

enum class StatusCode : uint8_t {
  kOk = 0,
  kNotFound,
  kAlreadyExists,
  kIOError,
  kInvalidArgument,
  kResourceExhausted,
  kUnavailable,
};

class Status {
 public:
  static Status OK() { return Status(StatusCode::kOk); }
  static Status NotFound(const char* msg = nullptr);
  static Status AlreadyExists(const char* msg = nullptr);
  static Status IOError(const char* msg = nullptr);
  static Status InvalidArgument(const char* msg = nullptr);
  static Status Unavailable(const char* msg = nullptr);
  static Status ResourceExhausted(const char* msg = nullptr);

  bool ok() const { return code_ == StatusCode::kOk; }
  StatusCode code() const { return code_; }
  const std::string& message() const { return message_; }

 private:
  Status(StatusCode code, const char* msg = nullptr);
  StatusCode code_;
  std::string message_;
};

}  // namespace fluxcache
