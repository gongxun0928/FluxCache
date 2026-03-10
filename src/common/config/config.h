#pragma once

#include "common/status.h"
#include <cstdint>
#include <optional>
#include <string>

namespace fluxcache {

struct MasterConfig {
  std::string host;
  uint16_t port = 0;
  std::string db_path = "./fluxcache_meta";  // RocksDB path for InodeStore
};

struct WorkerConfig {
  std::string data_dir;
  std::string host = "0.0.0.0";
  uint16_t port = 0;
  uint32_t heartbeat_interval_ms = 5000;
};

struct ClientConfig {
  std::string master_host;
  uint16_t master_port = 0;
};

struct UfsConfig {
  std::string type;   // "localfs" for Phase 1
  std::string path;
};

struct FluxCacheConfig {
  MasterConfig master;
  WorkerConfig worker;
  ClientConfig client;
  UfsConfig ufs;
};

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

StatusOr<FluxCacheConfig> LoadConfig(const std::string& path);

}  // namespace fluxcache
