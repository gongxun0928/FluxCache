#pragma once

#include "common/types.h"
#include <functional>
#include <optional>
#include <map>
#include <shared_mutex>
#include <string>
#include <vector>

namespace fluxcache {

// Endpoint info for GetRingSnapshot (host, port).
struct WorkerEndpointInfo {
  WorkerId worker_id = 0;
  std::string host;
  uint16_t port = 0;
};

// Callback to get Worker state for routing (skip SUSPECT when skip_suspect=true).
using WorkerStateProvider = std::function<WorkerState(WorkerId)>;

// Manages consistent hash ring for Block routing.
// Virtual nodes per worker (default 150). ring_version increments on topology change.
class HashRingManager {
 public:
  static constexpr int kDefaultVirtualNodes = 150;

  explicit HashRingManager(WorkerStateProvider state_provider = nullptr);

  // Set or update the state provider (e.g. from WorkerManager).
  void SetStateProvider(WorkerStateProvider provider);

  // Add worker to ring. Increments ring_version.
  void AddWorker(WorkerId worker_id);

  // Remove worker from ring. Increments ring_version.
  void RemoveWorker(WorkerId worker_id);

  // Deterministic routing: first ALIVE worker clockwise from block_id hash.
  // If skip_suspect=true (default), skips SUSPECT workers.
  WorkerId GetWorker(BlockId block_id, bool skip_suspect = true) const;

  // Return up to n ALIVE workers (skipping SUSPECT when skip_suspect=true).
  std::vector<WorkerId> GetCandidates(BlockId block_id, int n,
                                      bool skip_suspect = true) const;

  // Snapshot for Client sync: worker_ids in ring order with endpoint info.
  // Caller provides endpoint_lookup(worker_id) -> WorkerEndpointInfo.
  using EndpointLookup = std::function<std::optional<WorkerEndpointInfo>(WorkerId)>;
  struct RingSnapshot {
    uint64_t ring_version = 0;
    std::vector<WorkerEndpointInfo> workers;
  };
  RingSnapshot GetRingSnapshot(EndpointLookup endpoint_lookup) const;

  uint64_t GetVersion() const { return ring_version_; }

 private:
  uint64_t GetHash(const std::string& key) const;

  WorkerStateProvider state_provider_;
  mutable std::shared_mutex mu_;
  std::map<uint64_t, WorkerId> ring_;  // hash -> worker_id
  std::map<WorkerId, std::vector<uint64_t>> worker_to_hashes_;
  uint64_t ring_version_ = 0;
};

}  // namespace fluxcache
