#pragma once

#include "common/types.h"
#include <map>
#include <shared_mutex>
#include <string>

namespace fluxcache {

// Forward declaration for proto type.
namespace proto {
class GetHashRingResponse;
}

/// Client-side cached hash ring. Uses same FNV1a + virtual-node logic as
/// HashRingManager. Updated from GetHashRingResponse when ring_version changes.
class CachedHashRing {
 public:
  static constexpr int kDefaultVirtualNodes = 150;

  CachedHashRing() = default;

  /// Update ring from Master's GetHashRingResponse. Replaces current ring.
  void Update(const proto::GetHashRingResponse& resp);

  uint64_t GetVersion() const;

  /// Route block_id to Worker. Returns 0 if ring is empty.
  WorkerId GetWorker(BlockId block_id) const;

  /// Get "host:port" for worker_id. Returns empty string if not found.
  std::string GetWorkerAddress(WorkerId worker_id) const;

 private:
  uint64_t GetHash(const std::string& key) const;

  mutable std::shared_mutex mu_;
  uint64_t ring_version_ = 0;
  std::map<uint64_t, WorkerId> ring_;
  std::map<WorkerId, std::string> worker_to_address_;
};

}  // namespace fluxcache
