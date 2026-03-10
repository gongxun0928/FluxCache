#pragma once

#include "client/cached_hash_ring.h"
#include "client/master_client.h"
#include "client/worker_client.h"
#include "common/config/config.h"
#include "common/rpc/channel_pool.h"
#include "common/status.h"
#include <memory>

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

  MasterClient* GetMasterClient() { return master_client_.get(); }
  CachedHashRing* GetCachedHashRing() { return &cached_ring_; }

 private:
  std::string master_address_;
  ChannelPool pool_;
  std::unique_ptr<MasterClient> master_client_;
  CachedHashRing cached_ring_;
  bool ring_fetched_ = false;
};

}  // namespace fluxcache
