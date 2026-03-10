#include "common/status.h"

namespace fluxcache {

Status::Status(StatusCode code, const char* msg)
    : code_(code), message_(msg ? msg : "") {}

Status Status::NotFound(const char* msg) {
  return Status(StatusCode::kNotFound, msg);
}

Status Status::IOError(const char* msg) {
  return Status(StatusCode::kIOError, msg);
}

Status Status::InvalidArgument(const char* msg) {
  return Status(StatusCode::kInvalidArgument, msg ? msg : "invalid argument");
}

}  // namespace fluxcache
