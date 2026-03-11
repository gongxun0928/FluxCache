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
  /// When true, read path may return cached data with stale=true if UFS times out.
  bool allow_stale_read_on_ufs_timeout = false;
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
  /// Timeout for Master read RPCs (GetFileInfo, GetHashRing). Default 10s.
  int timeout_master_read_sec = 10;
  /// Timeout for Master write RPCs (CreateFile, CompleteFile, etc). Default 30s.
  int timeout_master_write_sec = 30;
  /// Timeout for Worker ReadPages. Default 10s.
  int timeout_worker_read_sec = 10;
  /// Timeout for Worker WritePages. Default 30s.
  int timeout_worker_write_sec = 30;
  /// Enable circuit breaker. Default true.
  bool circuit_breaker_enabled = true;
  /// Circuit breaker failure threshold. Default 5.
  int circuit_breaker_failure_threshold = 5;
  /// Circuit breaker error rate threshold (0.0-1.0). Default 0.5.
  double circuit_breaker_error_rate_threshold = 0.5;
  /// Circuit breaker open duration in ms. Default 30000.
  int circuit_breaker_open_duration_ms = 30000;
  /// Circuit breaker half-open probe successes to recover. Default 1.
  int circuit_breaker_half_open_probes = 1;
  /// Prefetch next N blocks during sequential read. 0 = disabled. Default 2.
  size_t prefetch_blocks = 2;
  /// Max blocks per BatchReadPages RPC. Default 8.
  size_t batch_read_max_blocks = 8;
  /// When true, client accepts stale read results (propagated from Worker).
  bool allow_read_degradation = false;
  /// Metrics HTTP port. 0 = disabled. When set, client exposes /metrics.
  uint16_t metrics_port = 0;
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
