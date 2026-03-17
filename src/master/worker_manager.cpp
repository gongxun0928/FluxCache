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
  std::vector<WorkerId> newly_dead;
  for (const auto& transition :
       CollectHealthTransitions(now_ms, heartbeat_timeout_ms, suspect_grace_ms)) {
    if (ApplyWorkerState(transition.worker_id, transition.state,
                         transition.last_heartbeat_ms,
                         transition.suspect_since_ms) &&
        transition.state == WorkerState::kDead) {
      newly_dead.push_back(transition.worker_id);
    }
  }
  return newly_dead;
}

std::vector<WorkerStateTransition> WorkerManager::CollectHealthTransitions(
    int64_t now_ms, int64_t heartbeat_timeout_ms,
    int64_t suspect_grace_ms) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<WorkerStateTransition> transitions;

  for (const auto& [worker_id, info] : workers_) {
    if (info.state == WorkerState::kDead) continue;

    int64_t elapsed = now_ms - info.last_heartbeat_ms;
    if (info.state == WorkerState::kAlive) {
      if (elapsed >= heartbeat_timeout_ms) {
        transitions.push_back(WorkerStateTransition{
            worker_id, WorkerState::kSuspect, info.last_heartbeat_ms, now_ms});
      }
      continue;
    }

    if (info.state == WorkerState::kSuspect) {
      if (info.suspect_since_ms <= 0) {
        transitions.push_back(WorkerStateTransition{
            worker_id, WorkerState::kSuspect, info.last_heartbeat_ms, now_ms});
        continue;
      }
      int64_t suspect_since_ms = info.suspect_since_ms;
      if ((now_ms - suspect_since_ms) >= suspect_grace_ms) {
        transitions.push_back(WorkerStateTransition{
            worker_id, WorkerState::kDead, info.last_heartbeat_ms,
            suspect_since_ms});
      }
    }
  }

  return transitions;
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

void WorkerManager::ApplyWorkerRegistration(WorkerId worker_id,
                                            const std::string& host,
                                            uint16_t port,
                                            int64_t last_heartbeat_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  auto& info = workers_[worker_id];
  info.worker_id = worker_id;
  info.host = host;
  info.port = port;
  info.state = WorkerState::kAlive;
  info.last_heartbeat_ms = last_heartbeat_ms;
  info.suspect_since_ms = 0;
}

bool WorkerManager::ApplyWorkerState(WorkerId worker_id, WorkerState state,
                                     int64_t last_heartbeat_ms,
                                     int64_t suspect_since_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = workers_.find(worker_id);
  if (it == workers_.end()) return false;
  it->second.state = state;
  it->second.last_heartbeat_ms = last_heartbeat_ms;
  it->second.suspect_since_ms = suspect_since_ms;
  return true;
}

std::vector<WorkerInfo> WorkerManager::GetAllWorkers() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<WorkerInfo> result;
  result.reserve(workers_.size());
  for (const auto& [_, info] : workers_) {
    result.push_back(info);
  }
  std::sort(result.begin(), result.end(),
            [](const WorkerInfo& a, const WorkerInfo& b) {
              return a.worker_id < b.worker_id;
            });
  return result;
}

void WorkerManager::ReplaceAllWorkers(const std::vector<WorkerInfo>& workers) {
  std::lock_guard<std::mutex> lock(mu_);
  workers_.clear();
  for (const auto& info : workers) {
    workers_[info.worker_id] = info;
  }
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
