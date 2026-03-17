#pragma once

#include "common/types.h"
#include <map>
#include <optional>
#include <mutex>
#include <string>
#include <vector>

namespace fluxcache {

// Worker info for registration and heartbeat.
struct WorkerInfo {
  WorkerId worker_id = 0;
  std::string host;
  uint16_t port = 0;
  WorkerState state = WorkerState::kAlive;
  int64_t last_heartbeat_ms = 0;
  int64_t suspect_since_ms = 0;  // When entered SUSPECT (for grace period)
};

struct WorkerStateTransition {
  WorkerId worker_id = 0;
  WorkerState state = WorkerState::kAlive;
  int64_t last_heartbeat_ms = 0;
  int64_t suspect_since_ms = 0;
};

// Manages Worker registration, heartbeat, and ALIVE/SUSPECT/DEAD state machine.
// Timeout parameters are injectable for testability.
class WorkerManager {
 public:
  WorkerManager() = default;

  // Register a new Worker. Enters ALIVE state.
  // If worker_id already exists, updates host/port and last_heartbeat (idempotent refresh).
  bool RegisterWorker(WorkerId worker_id, const std::string& host, uint16_t port,
                     int64_t now_ms);

  // Update last heartbeat time. If SUSPECT, transitions back to ALIVE.
  void HandleHeartbeat(WorkerId worker_id, int64_t now_ms);

  // Drive state transitions. Returns WorkerIds that newly entered DEAD.
  // heartbeat_timeout_ms: no heartbeat for this long -> SUSPECT
  // suspect_grace_ms: in SUSPECT for this long -> DEAD
  std::vector<WorkerId> CheckWorkerHealth(int64_t now_ms,
                                          int64_t heartbeat_timeout_ms,
                                          int64_t suspect_grace_ms);
  std::vector<WorkerStateTransition> CollectHealthTransitions(
      int64_t now_ms, int64_t heartbeat_timeout_ms,
      int64_t suspect_grace_ms) const;

  // Get worker info if exists.
  std::optional<WorkerInfo> GetWorker(WorkerId worker_id) const;

  WorkerState GetWorkerState(WorkerId worker_id) const;

  // Deterministic apply helpers for replicated topology state.
  void ApplyWorkerRegistration(WorkerId worker_id, const std::string& host,
                               uint16_t port, int64_t last_heartbeat_ms);
  bool ApplyWorkerState(WorkerId worker_id, WorkerState state,
                        int64_t last_heartbeat_ms,
                        int64_t suspect_since_ms);
  std::vector<WorkerInfo> GetAllWorkers() const;
  void ReplaceAllWorkers(const std::vector<WorkerInfo>& workers);

  // Returns all workers that are in the ring (ALIVE + SUSPECT, not DEAD).
  std::vector<WorkerInfo> GetAllWorkersInRing() const;

 private:
  mutable std::mutex mu_;
  std::map<WorkerId, WorkerInfo> workers_;
};

}  // namespace fluxcache
