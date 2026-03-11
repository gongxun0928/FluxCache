#pragma once

#include "common/config/config.h"
#include "common/rpc/channel_pool.h"
#include "common/status.h"
#include "master.pb.h"
#include <memory>
#include <optional>
#include <string>

namespace fluxcache {

/// Master RPC wrapper. All RPCs set deadline.
class MasterClient {
 public:
  /// @param pool Channel pool (must outlive this client).
  /// @param master_address "host:port".
  /// @param deadline_sec RPC deadline in seconds (default 10).
  MasterClient(ChannelPool* pool, const std::string& master_address,
               int deadline_sec = 10);

  /// Get hash ring from Master. Returns Unavailable on DEADLINE_EXCEEDED or
  /// UNAVAILABLE.
  StatusOr<proto::GetHashRingResponse> GetHashRing();

  /// Get file metadata from Master. Returns NotFound if path not found,
  /// Unavailable on DEADLINE_EXCEEDED or UNAVAILABLE.
  StatusOr<proto::GetFileInfoResponse> GetFileInfo(const std::string& path);

  /// Create new file at path. Returns AlreadyExists if path exists.
  StatusOr<proto::CreateFileResponse> CreateFile(const std::string& path);

  /// Complete file write: update size and mtime. Call after WritePages succeed.
  Status CompleteFile(uint64_t inode_id, uint64_t size,
                     std::optional<int64_t> ufs_mtime_ms = std::nullopt);

  /// Mount UFS at path. For test/setup.
  Status Mount(const std::string& path, const std::string& ufs_uri);

  /// Register Worker. Returns worker_id. For test/setup.
  StatusOr<uint64_t> RegisterWorker(const std::string& host, uint16_t port);

 private:
  ChannelPool* pool_;
  std::string master_address_;
  int deadline_sec_;
};

}  // namespace fluxcache
