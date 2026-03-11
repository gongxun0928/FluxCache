#pragma once

#include "common/status.h"
#include "common/status_or.h"
#include <cstdint>
#include <optional>
#include <string>

namespace fluxcache {

struct MasterConfig {
  std::string host;
  uint16_t port = 0;
  std::string db_path = "./fluxcache_meta";  // RocksDB path for InodeStore
  uint16_t metrics_port = 0;  // 0 = disabled
};

struct WorkerConfig {
  std::string data_dir;
  std::string metastore_path;  // RocksDB path; default data_dir/metastore
  std::string host = "0.0.0.0";
  uint16_t port = 0;
  uint32_t heartbeat_interval_ms = 5000;
  uint16_t metrics_port = 0;  // 0 = disabled
  /// Eviction policy: "lru" or "lfu". Default "lru".
  std::string eviction_policy = "lru";
};

struct ClientConfig {
  std::string master_host;
  uint16_t master_port = 0;
  /// Page size for block/page slicing. Must match Worker config. Default 1MB.
  size_t page_size = 1024 * 1024;
  /// Channels per address in ChannelPool. Default 4.
  size_t channel_pool_size = 4;
  /// Max retry attempts for idempotent RPCs. Default 3.
  int retry_max_attempts = 3;
  /// Initial delay between retries in ms. Default 50.
  int retry_initial_delay_ms = 50;
  /// Enable L1 page cache. Default true.
  bool local_cache_enabled = true;
  /// L1 cache capacity in bytes. 0 = disabled. Default 256MB.
  size_t local_cache_size_bytes = 256 * 1024 * 1024;
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

StatusOr<FluxCacheConfig> LoadConfig(const std::string& path);

}  // namespace fluxcache
