#pragma once

#include "common/status.h"
#include <optional>

namespace fluxcache {

template <typename T>
class StatusOr {
 public:
  StatusOr(T value) : status_(Status::OK()), value_(std::move(value)) {}
  StatusOr(Status status) : status_(std::move(status)) {}
  bool ok() const { return status_.ok(); }
  const T& value() const { return *value_; }
  T& value() { return *value_; }
  const Status& status() const { return status_; }

 private:
  Status status_;
  std::optional<T> value_;
};

}  // namespace fluxcache
