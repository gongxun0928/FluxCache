#pragma once

#include "common/status.h"
#include "common/status_or.h"
#include <cstdint>
#include <string>

namespace fluxcache {

/// SDK configuration. Maps to ClientConfig internally.
struct SDKConfig {
  std::string master_host;
  uint16_t master_port = 0;
  size_t page_size = 1024 * 1024;
  size_t channel_pool_size = 4;
  int retry_max_attempts = 3;
  int retry_initial_delay_ms = 50;
  /// Enable L1 page cache. Default true.
  bool local_cache_enabled = true;
  /// L1 cache capacity in MB. 0 = disabled. Default 256.
  size_t local_cache_size_mb = 256;
};

/// File metadata returned by Stat. No gRPC/protobuf types.
struct FileInfo {
  uint64_t inode_id = 0;
  uint64_t size = 0;
  bool is_directory = false;
  int64_t ufs_mtime_ms = 0;
};

enum class OpenMode {
  kReadOnly,
  kWriteOnly,
  kReadWrite,
};

}  // namespace fluxcache
