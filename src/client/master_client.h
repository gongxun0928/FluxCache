#pragma once

#include "common/rpc/channel_pool.h"
#include "common/rpc/circuit_breaker.h"
#include "common/rpc/resilience_config.h"
#include "common/rpc/retry_policy.h"
#include "common/status.h"
#include "master.pb.h"
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace fluxcache {

class MetricsRegistry;

/// Master RPC wrapper. All RPCs set deadline per op type; optional circuit breaker.
class MasterClient {
 public:
  /// @param pool Channel pool (must outlive this client).
  /// @param master_address "host:port".
  /// @param resilience_config Timeouts per op type.
  /// @param retry_policy Retry policy for idempotent RPCs (default: 3 retries).
  /// @param circuit_breaker Optional; if non-null and enabled, blocks when OPEN.
  /// @param metrics Optional; for retry/breaker observability.
  MasterClient(ChannelPool* pool, const std::string& master_address,
               const ResilienceConfig& resilience_config,
               const RetryPolicy& retry_policy = RetryPolicy{},
               CircuitBreaker* circuit_breaker = nullptr,
               MetricsRegistry* metrics = nullptr);

  /// Get hash ring from Master. Returns Unavailable on DEADLINE_EXCEEDED or
  /// UNAVAILABLE.
  StatusOr<proto::GetHashRingResponse> GetHashRing();

  /// Get file metadata from Master. Returns NotFound if path not found,
  /// Unavailable on DEADLINE_EXCEEDED or UNAVAILABLE.
  StatusOr<proto::GetFileInfoResponse> GetFileInfo(const std::string& path);

  /// Create new file at path. Returns AlreadyExists if path exists.
  StatusOr<proto::CreateFileResponse> CreateFile(const std::string& path);

  /// Complete file write: update size and increment file_version. Call after
  /// WritePages succeed.
  Status CompleteFile(uint64_t inode_id, uint64_t size);

  /// Delete file at path. Returns NotFound if path not found.
  /// Server may return Unavailable/IOError if DeleteFile not yet implemented.
  Status DeleteFile(const std::string& path);

  /// Mount UFS at path. For test/setup.
  Status Mount(const std::string& path, const std::string& ufs_uri);

  /// Unmount path. Returns NotFound if path not mounted.
  Status Unmount(const std::string& path);

  /// List all mount paths. Returns empty vector if none.
  StatusOr<std::vector<std::string>> ListMounts();

  /// Register Worker. Returns worker_id. For test/setup.
  StatusOr<uint64_t> RegisterWorker(const std::string& host, uint16_t port);

  /// Create directory at path. Returns FileInfo of the new directory.
  StatusOr<proto::MkdirResponse> Mkdir(const std::string& path);

  /// Remove empty directory at path.
  Status Rmdir(const std::string& path);

  /// List directory contents. Returns DirEntry list.
  StatusOr<proto::ListDirResponse> ListDir(const std::string& path);

  /// Rename src_path to dst_path. Overwrites dst if exists (non-dir or empty dir).
  Status Rename(const std::string& src_path, const std::string& dst_path);

  /// Stat path. Returns inode_id and FileInfo.
  StatusOr<proto::StatResponse> Stat(const std::string& path);

 private:
  std::string GetMasterAddress() const;
  bool TryFollowLeaderHint(const grpc::Status& status);

  ChannelPool* pool_;
  mutable std::mutex master_address_mu_;
  std::string master_address_;
  ResilienceConfig resilience_config_;
  RetryPolicy retry_policy_;
  CircuitBreaker* circuit_breaker_;
  MetricsRegistry* metrics_;
};

}  // namespace fluxcache
