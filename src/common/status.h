#pragma once

#include <cstdint>

namespace fluxcache {

enum class StatusCode : uint8_t {
  kOk = 0,
  kNotFound,
  kIOError,
  kInvalidArgument,
};

class Status {
 public:
  static Status OK() { return Status(StatusCode::kOk); }
  static Status NotFound(const char* msg = nullptr);
  static Status IOError(const char* msg = nullptr);

  bool ok() const { return code_ == StatusCode::kOk; }
  StatusCode code() const { return code_; }

 private:
  explicit Status(StatusCode code) : code_(code) {}
  StatusCode code_;
};

}  // namespace fluxcache
