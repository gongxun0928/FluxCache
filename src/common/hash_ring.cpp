#include "common/hash_ring.h"
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

SimpleHashRing::SimpleHashRing(const std::vector<WorkerId>& worker_ids) {
  for (WorkerId wid : worker_ids) {
    for (int i = 0; i < kDefaultVirtualNodes; ++i) {
      std::ostringstream oss;
      oss << wid << "#" << i;
      uint64_t h = Fnv1aHash(oss.str());
      ring_[h] = wid;
    }
  }
}

uint64_t SimpleHashRing::Hash(const std::string& key) const {
  return Fnv1aHash(key);
}

WorkerId SimpleHashRing::GetWorker(BlockId block_id) const {
  if (ring_.empty()) return 0;

  std::string key = std::to_string(block_id);
  uint64_t h = Hash(key);

  auto it = ring_.upper_bound(h);
  if (it == ring_.end()) it = ring_.begin();

  return it->second;
}

}  // namespace fluxcache
