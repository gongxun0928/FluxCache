#include "common/status.h"

namespace fluxcache {

Status Status::NotFound(const char* /*msg*/) {
  return Status(StatusCode::kNotFound);
}

Status Status::IOError(const char* /*msg*/) {
  return Status(StatusCode::kIOError);
}

}  // namespace fluxcache
