#pragma once

#include "worker/cache/eviction_policy.h"

#include <list>
#include <vector>
#include <unordered_map>

namespace fluxcache {

// LRU (Least Recently Used) eviction policy. Uses doubly-linked list + hash map
// for O(1) OnInsert, OnAccess, OnRemove, and PickVictim.
class LruPolicy : public EvictionPolicy {
 public:
  void OnInsert(PageId id) override;
  void OnAccess(PageId id) override;
  void OnRemove(PageId id) override;
  std::optional<PageId> PickVictim() override;
  std::vector<PageId> GetOrderedFromMru() const override;

 private:
  std::list<PageId> order_;  // front = MRU, back = LRU
  std::unordered_map<PageId, std::list<PageId>::iterator> index_;
};

}  // namespace fluxcache
