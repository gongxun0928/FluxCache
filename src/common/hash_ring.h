#pragma once

#include "common/types.h"
#include <map>
#include <string>
#include <vector>

namespace fluxcache {

// Lightweight hash ring for block-to-worker routing. Matches HashRingManager
// algorithm (FNV-1a, 150 virtual nodes per worker) for consistency.
class SimpleHashRing {
 public:
  static constexpr int kDefaultVirtualNodes = 150;

  explicit SimpleHashRing(const std::vector<WorkerId>& worker_ids);

  // Returns the WorkerId that owns the given block_id.
  WorkerId GetWorker(BlockId block_id) const;

 private:
  uint64_t Hash(const std::string& key) const;

  std::map<uint64_t, WorkerId> ring_;
};

}  // namespace fluxcache
