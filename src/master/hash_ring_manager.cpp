#include "master/hash_ring_manager.h"
#include "master/worker_manager.h"
#include <algorithm>
#include <set>
#include <sstream>

namespace fluxcache {

namespace {

inline uint64_t Fnv1aHash(const std::string& key) {
  uint64_t hash = 14695981039346656037ULL;  // FNV offset basis
  for (unsigned char c : key) {
    hash ^= c;
    hash *= 1099511628211ULL;  // FNV prime
  }
  return hash;
}

}  // namespace

HashRingManager::HashRingManager(WorkerStateProvider state_provider)
    : state_provider_(std::move(state_provider)) {}

void HashRingManager::SetStateProvider(WorkerStateProvider provider) {
  std::unique_lock lock(mu_);
  state_provider_ = std::move(provider);
}

uint64_t HashRingManager::GetHash(const std::string& key) const {
  return Fnv1aHash(key);
}

void HashRingManager::AddWorker(WorkerId worker_id) {
  std::unique_lock lock(mu_);
  if (worker_to_hashes_.count(worker_id)) return;  // Already present

  auto& hashes = worker_to_hashes_[worker_id];
  for (int i = 0; i < kDefaultVirtualNodes; ++i) {
    std::ostringstream oss;
    oss << worker_id << "#" << i;
    uint64_t h = GetHash(oss.str());
    ring_[h] = worker_id;
    hashes.push_back(h);
  }
  ++ring_version_;
}

void HashRingManager::RemoveWorker(WorkerId worker_id) {
  std::unique_lock lock(mu_);
  auto it = worker_to_hashes_.find(worker_id);
  if (it == worker_to_hashes_.end()) return;

  for (uint64_t h : it->second) {
    ring_.erase(h);
  }
  worker_to_hashes_.erase(it);
  ++ring_version_;
}

bool HashRingManager::ContainsWorker(WorkerId worker_id) const {
  std::shared_lock lock(mu_);
  return worker_to_hashes_.count(worker_id) > 0;
}

uint64_t HashRingManager::GetVersion() const {
  std::shared_lock lock(mu_);
  return ring_version_;
}

WorkerId HashRingManager::GetWorker(BlockId block_id, bool skip_suspect) const {
  std::shared_lock lock(mu_);
  if (ring_.empty()) return 0;

  std::string key = std::to_string(block_id);
  uint64_t h = GetHash(key);

  auto it = ring_.upper_bound(h);
  if (it == ring_.end()) it = ring_.begin();

  int count = 0;
  const int max_iter = static_cast<int>(ring_.size());
  while (count < max_iter) {
    WorkerId wid = it->second;
    if (state_provider_) {
      WorkerState s = state_provider_(wid);
      if (s == WorkerState::kDead) {
        ++it;
        if (it == ring_.end()) it = ring_.begin();
        ++count;
        continue;
      }
      if (skip_suspect && s == WorkerState::kSuspect) {
        ++it;
        if (it == ring_.end()) it = ring_.begin();
        ++count;
        continue;
      }
    }
    return wid;
  }
  return 0;
}

std::vector<WorkerId> HashRingManager::GetCandidates(BlockId block_id, int n,
                                                     bool skip_suspect) const {
  std::shared_lock lock(mu_);
  std::vector<WorkerId> result;
  if (ring_.empty() || n <= 0) return result;

  std::string key = std::to_string(block_id);
  uint64_t h = GetHash(key);

  auto it = ring_.upper_bound(h);
  if (it == ring_.end()) it = ring_.begin();

  std::set<WorkerId> seen;
  int count = 0;
  const int max_iter = static_cast<int>(ring_.size());
  while (result.size() < static_cast<size_t>(n) && count < max_iter) {
    WorkerId wid = it->second;
    if (state_provider_) {
      WorkerState s = state_provider_(wid);
      if (s == WorkerState::kDead) {
        ++it;
        if (it == ring_.end()) it = ring_.begin();
        ++count;
        continue;
      }
      if (skip_suspect && s == WorkerState::kSuspect) {
        ++it;
        if (it == ring_.end()) it = ring_.begin();
        ++count;
        continue;
      }
    }
    if (seen.insert(wid).second) {
      result.push_back(wid);
    }
    ++it;
    if (it == ring_.end()) it = ring_.begin();
    ++count;
  }
  return result;
}

HashRingManager::RingSnapshot HashRingManager::GetRingSnapshot(
    EndpointLookup endpoint_lookup) const {
  std::shared_lock lock(mu_);
  RingSnapshot snap;
  snap.ring_version = ring_version_;

  std::set<WorkerId> seen;
  for (const auto& [_, worker_id] : ring_) {
    if (seen.insert(worker_id).second) {
      if (endpoint_lookup) {
        auto ep = endpoint_lookup(worker_id);
        if (ep) {
          WorkerEndpointInfo info;
          info.worker_id = ep->worker_id;
          info.host = ep->host;
          info.port = ep->port;
          snap.workers.push_back(info);
        }
      } else {
        WorkerEndpointInfo info;
        info.worker_id = worker_id;
        info.host = "";
        info.port = 0;
        snap.workers.push_back(info);
      }
    }
  }
  std::sort(snap.workers.begin(), snap.workers.end(),
            [](const WorkerEndpointInfo& a, const WorkerEndpointInfo& b) {
              return a.worker_id < b.worker_id;
            });
  return snap;
}

void HashRingManager::RestoreFromWorkers(const std::vector<WorkerInfo>& workers,
                                         uint64_t ring_version) {
  std::unique_lock lock(mu_);
  ring_.clear();
  worker_to_hashes_.clear();
  for (const auto& info : workers) {
    if (info.state == WorkerState::kDead) continue;
    auto& hashes = worker_to_hashes_[info.worker_id];
    for (int i = 0; i < kDefaultVirtualNodes; ++i) {
      std::ostringstream oss;
      oss << info.worker_id << "#" << i;
      uint64_t h = GetHash(oss.str());
      ring_[h] = info.worker_id;
      hashes.push_back(h);
    }
  }
  ring_version_ = ring_version;
}

}  // namespace fluxcache
