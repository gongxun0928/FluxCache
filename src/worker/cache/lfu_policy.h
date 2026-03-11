#pragma once

#include "worker/cache/eviction_policy.h"

#include <list>
#include <map>
#include <unordered_map>

namespace fluxcache {

// LFU (Least Frequently Used) eviction policy. Evicts pages with lowest access
// count; ties broken by LRU (least recently used). Uses freq buckets + hash map
// for O(1) OnInsert, OnAccess, OnRemove, and O(1) PickVictim (min freq bucket).
class LfuPolicy : public EvictionPolicy {
 public:
  void OnInsert(PageId id) override;
  void OnAccess(PageId id) override;
  void OnRemove(PageId id) override;
  std::optional<PageId> PickVictim() override;
  std::vector<PageId> GetOrderedFromMru() const override;

 private:
  // freq -> list of PageIds in LRU order (front=MRU, back=LRU)
  std::map<uint64_t, std::list<PageId>> freq_buckets_;
  // PageId -> (freq, iterator into freq_buckets_[freq])
  std::unordered_map<PageId,
                    std::pair<uint64_t, std::list<PageId>::iterator>>
      index_;
};

}  // namespace fluxcache
