#pragma once

#include "client/cached_hash_ring.h"
#include "client/master_client.h"
#include "client/worker_client.h"
#include "common/config/config.h"
#include "common/rpc/channel_pool.h"
#include "common/rpc/retry_policy.h"
#include "common/status.h"
#include <memory>
#include <string_view>

namespace fluxcache {

/// Client facade: ChannelPool, MasterClient, CachedHashRing, Worker routing.
class FluxCacheClient {
 public:
  explicit FluxCacheClient(const ClientConfig& config);

  /// Refresh ring from Master if needed, then return Worker for block_id.
  /// Returns NotFound if no worker in ring.
  StatusOr<WorkerId> GetWorkerForBlock(BlockId block_id);

  /// Get WorkerClient for worker_id. Returns NotFound if worker not in ring,
  /// Unavailable if Master unreachable (ring not yet fetched).
  StatusOr<std::unique_ptr<WorkerClient>> GetWorkerClient(WorkerId worker_id);

  /// Force refresh ring from Master. Returns Unavailable if Master unreachable.
  Status RefreshRing();

  /// Read file data. Returns data at [offset, offset+size), clamped to file end.
  /// Uses GetFileInfo, block/page split, Worker ReadPages, and retry on failure.
  StatusOr<std::string> Read(const std::string& path, uint64_t offset,
                            uint64_t size);

  /// Write data at [offset, offset+data.size()). New file: CreateFile; existing:
  /// GetFileInfo. Splits by block/page, calls WritePages, then CompleteFile.
  /// On WritePages failure, does NOT call CompleteFile.
  Status Write(const std::string& path, uint64_t offset,
               std::string_view data);

  /// Delete file at path. Returns NotFound if path not found.
  /// May return Unavailable if server DeleteFile not yet implemented.
  Status Delete(const std::string& path);

  MasterClient* GetMasterClient() { return master_client_.get(); }
  CachedHashRing* GetCachedHashRing() { return &cached_ring_; }

  /// Test hook: inject ring without calling Master. For "no worker" acceptance.
  void SetRingForTest(const proto::GetHashRingResponse& resp);

 private:
  std::string master_address_;
  size_t page_size_;
  RetryPolicy retry_policy_;
  ChannelPool pool_;
  std::unique_ptr<MasterClient> master_client_;
  CachedHashRing cached_ring_;
  bool ring_fetched_ = false;
};

}  // namespace fluxcache
