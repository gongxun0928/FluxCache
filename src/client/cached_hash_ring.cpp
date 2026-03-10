#include "client/cached_hash_ring.h"
#include "master.pb.h"
#include <algorithm>
#include <sstream>

namespace fluxcache {

namespace {

inline uint64_t Fnv1aHash(const std::string& key) {
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : key) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

}  // namespace

uint64_t CachedHashRing::GetHash(const std::string& key) const {
  return Fnv1aHash(key);
}

void CachedHashRing::Update(const proto::GetHashRingResponse& resp) {
  std::unique_lock lock(mu_);
  ring_.clear();
  worker_to_address_.clear();
  ring_version_ = resp.ring_version();

  for (const auto& ep : resp.workers()) {
    WorkerId wid = ep.worker_id();
    std::string addr = ep.host() + ":" + std::to_string(ep.port());
    worker_to_address_[wid] = addr;

    for (int i = 0; i < kDefaultVirtualNodes; ++i) {
      std::ostringstream oss;
      oss << wid << "#" << i;
      uint64_t h = GetHash(oss.str());
      ring_[h] = wid;
    }
  }
}

uint64_t CachedHashRing::GetVersion() const {
  std::shared_lock lock(mu_);
  return ring_version_;
}

WorkerId CachedHashRing::GetWorker(BlockId block_id) const {
  std::shared_lock lock(mu_);
  if (ring_.empty()) return 0;

  std::string key = std::to_string(block_id);
  uint64_t h = GetHash(key);

  auto it = ring_.upper_bound(h);
  if (it == ring_.end()) it = ring_.begin();
  return it->second;
}

std::string CachedHashRing::GetWorkerAddress(WorkerId worker_id) const {
  std::shared_lock lock(mu_);
  auto it = worker_to_address_.find(worker_id);
  if (it == worker_to_address_.end()) return "";
  return it->second;
}

}  // namespace fluxcache
