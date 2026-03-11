#include "client/cache/client_page_cache.h"

namespace fluxcache {

ClientPageCache::ClientPageCache(size_t max_size_bytes)
    : max_size_bytes_(max_size_bytes) {}

std::shared_ptr<const std::vector<uint8_t>> ClientPageCache::Get(
    PageId page_id, int64_t expected_mtime_ms) {
  std::unique_lock lock(mu_);
  auto it = pages_.find(page_id);
  if (it == pages_.end()) {
    return nullptr;
  }
  CachedPage& entry = it->second.first;
  if (entry.cached_mtime_ms != expected_mtime_ms) {
    RemoveLocked(page_id);
    return nullptr;
  }
  // Move to MRU
  lru_order_.erase(it->second.second);
  lru_order_.push_front(page_id);
  it->second.second = lru_order_.begin();
  return entry.data;
}

void ClientPageCache::Put(PageId page_id, std::vector<uint8_t> data,
                         int64_t mtime_ms) {
  size_t page_bytes = data.size();
  if (page_bytes == 0) return;

  std::unique_lock lock(mu_);
  auto it = pages_.find(page_id);
  if (it != pages_.end()) {
    current_size_bytes_ -= it->second.first.data->size();
    lru_order_.erase(it->second.second);
    pages_.erase(it);
  }

  EvictUntilFit(page_bytes);

  auto shared = std::make_shared<std::vector<uint8_t>>(std::move(data));
  CachedPage entry;
  entry.data = shared;
  entry.cached_mtime_ms = mtime_ms;

  lru_order_.push_front(page_id);
  pages_[page_id] = {entry, lru_order_.begin()};
  current_size_bytes_ += page_bytes;
}

void ClientPageCache::Invalidate(PageId page_id) {
  std::unique_lock lock(mu_);
  RemoveLocked(page_id);
}

void ClientPageCache::InvalidateFile(InodeId inode_id) {
  std::unique_lock lock(mu_);
  std::vector<PageId> to_remove;
  for (const auto& [pid, _] : pages_) {
    if (GetInodeId(pid.block_id) == inode_id) {
      to_remove.push_back(pid);
    }
  }
  for (PageId pid : to_remove) {
    RemoveLocked(pid);
  }
}

void ClientPageCache::Clear() {
  std::unique_lock lock(mu_);
  pages_.clear();
  lru_order_.clear();
  current_size_bytes_ = 0;
}

size_t ClientPageCache::Size() const {
  std::shared_lock lock(mu_);
  return current_size_bytes_;
}

size_t ClientPageCache::Capacity() const { return max_size_bytes_; }

void ClientPageCache::EvictUntilFit(size_t required_bytes) {
  while (!lru_order_.empty() &&
         current_size_bytes_ + required_bytes > max_size_bytes_) {
    PageId victim = lru_order_.back();
    RemoveLocked(victim);
  }
}

void ClientPageCache::RemoveLocked(PageId page_id) {
  auto it = pages_.find(page_id);
  if (it == pages_.end()) return;
  current_size_bytes_ -= it->second.first.data->size();
  lru_order_.erase(it->second.second);
  pages_.erase(it);
}

}  // namespace fluxcache
