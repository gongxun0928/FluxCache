#include "worker/cache/lru_policy.h"

namespace fluxcache {

void LruPolicy::OnInsert(PageId id) {
  order_.push_front(id);
  index_[id] = order_.begin();
}

void LruPolicy::OnAccess(PageId id) {
  auto it = index_.find(id);
  if (it == index_.end()) return;
  order_.erase(it->second);
  order_.push_front(id);
  index_[id] = order_.begin();
}

void LruPolicy::OnRemove(PageId id) {
  auto it = index_.find(id);
  if (it == index_.end()) return;
  order_.erase(it->second);
  index_.erase(it);
}

std::optional<PageId> LruPolicy::PickVictim() {
  if (order_.empty()) return std::nullopt;
  return order_.back();
}

std::vector<PageId> LruPolicy::GetOrderedFromMru() const {
  return std::vector<PageId>(order_.begin(), order_.end());
}

}  // namespace fluxcache
