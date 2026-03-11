#include "worker/cache/lfu_policy.h"

namespace fluxcache {

void LfuPolicy::OnInsert(PageId id) {
  freq_buckets_[1].push_front(id);
  index_[id] = {1, freq_buckets_[1].begin()};
}

void LfuPolicy::OnAccess(PageId id) {
  auto it = index_.find(id);
  if (it == index_.end()) return;

  uint64_t old_freq = it->second.first;
  auto list_it = it->second.second;

  freq_buckets_[old_freq].erase(list_it);
  if (freq_buckets_[old_freq].empty()) {
    freq_buckets_.erase(old_freq);
  }

  uint64_t new_freq = old_freq + 1;
  freq_buckets_[new_freq].push_front(id);
  index_[id] = {new_freq, freq_buckets_[new_freq].begin()};
}

void LfuPolicy::OnRemove(PageId id) {
  auto it = index_.find(id);
  if (it == index_.end()) return;

  uint64_t freq = it->second.first;
  auto list_it = it->second.second;

  freq_buckets_[freq].erase(list_it);
  if (freq_buckets_[freq].empty()) {
    freq_buckets_.erase(freq);
  }
  index_.erase(it);
}

std::optional<PageId> LfuPolicy::PickVictim() {
  if (freq_buckets_.empty()) return std::nullopt;
  auto& min_bucket = freq_buckets_.begin()->second;
  if (min_bucket.empty()) return std::nullopt;
  return min_bucket.back();
}

std::vector<PageId> LfuPolicy::GetOrderedFromMru() const {
  std::vector<PageId> result;
  for (auto it = freq_buckets_.rbegin(); it != freq_buckets_.rend(); ++it) {
    for (const PageId& id : it->second) {
      result.push_back(id);
    }
  }
  return result;
}

}  // namespace fluxcache
