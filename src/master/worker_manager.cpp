#include "master/worker_manager.h"
#include <algorithm>

namespace fluxcache {

bool WorkerManager::RegisterWorker(WorkerId worker_id, const std::string& host,
                                   uint16_t port, int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = workers_.find(worker_id);
  if (it != workers_.end()) {
    it->second.host = host;
    it->second.port = port;
    it->second.last_heartbeat_ms = now_ms;
    if (it->second.state == WorkerState::kSuspect) {
      it->second.state = WorkerState::kAlive;
      it->second.suspect_since_ms = 0;
    }
    return true;
  }
  WorkerInfo info;
  info.worker_id = worker_id;
  info.host = host;
  info.port = port;
  info.state = WorkerState::kAlive;
  info.last_heartbeat_ms = now_ms;
  workers_[worker_id] = info;
  return false;
}

void WorkerManager::HandleHeartbeat(WorkerId worker_id, int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = workers_.find(worker_id);
  if (it == workers_.end()) return;
  it->second.last_heartbeat_ms = now_ms;
  if (it->second.state == WorkerState::kSuspect) {
    it->second.state = WorkerState::kAlive;
    it->second.suspect_since_ms = 0;
  }
}

std::vector<WorkerId> WorkerManager::CheckWorkerHealth(
    int64_t now_ms, int64_t heartbeat_timeout_ms, int64_t suspect_grace_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<WorkerId> newly_dead;

  for (auto& [worker_id, info] : workers_) {
    if (info.state == WorkerState::kDead) continue;

    int64_t elapsed = now_ms - info.last_heartbeat_ms;

    if (info.state == WorkerState::kAlive) {
      if (elapsed >= heartbeat_timeout_ms) {
        info.state = WorkerState::kSuspect;
        info.suspect_since_ms = now_ms;
      }
    } else if (info.state == WorkerState::kSuspect) {
      int64_t suspect_elapsed = now_ms - info.suspect_since_ms;
      if (suspect_elapsed >= suspect_grace_ms) {
        info.state = WorkerState::kDead;
        newly_dead.push_back(worker_id);
      }
    }
  }

  return newly_dead;
}

std::optional<WorkerInfo> WorkerManager::GetWorker(WorkerId worker_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = workers_.find(worker_id);
  if (it == workers_.end()) return std::nullopt;
  return it->second;
}

WorkerState WorkerManager::GetWorkerState(WorkerId worker_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = workers_.find(worker_id);
  if (it == workers_.end()) return WorkerState::kDead;
  return it->second.state;
}

std::vector<WorkerInfo> WorkerManager::GetAllWorkersInRing() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<WorkerInfo> result;
  for (const auto& [_, info] : workers_) {
    if (info.state != WorkerState::kDead) {
      result.push_back(info);
    }
  }
  std::sort(result.begin(), result.end(),
            [](const WorkerInfo& a, const WorkerInfo& b) {
              return a.worker_id < b.worker_id;
            });
  return result;
}

}  // namespace fluxcache
