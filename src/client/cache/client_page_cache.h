#pragma once

#include "common/types.h"

#include <cstdint>
#include <list>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace fluxcache {

/// Client-side L1 Page memory cache with mtime validation and LRU eviction.
/// Thread-safe via std::shared_mutex.
class ClientPageCache {
 public:
  explicit ClientPageCache(size_t max_size_bytes);

  /// Get cached page. Returns data if hit and mtime matches; otherwise returns
  /// nullptr and evicts stale entry if present.
  std::shared_ptr<const std::vector<uint8_t>> Get(PageId page_id,
                                                  int64_t expected_mtime_ms);

  /// Put page into cache. May trigger LRU eviction when over capacity.
  void Put(PageId page_id, std::vector<uint8_t> data, int64_t mtime_ms);

  /// Invalidate single page.
  void Invalidate(PageId page_id);

  /// Invalidate all pages belonging to file (by inode_id).
  void InvalidateFile(InodeId inode_id);

  /// Clear all cached pages.
  void Clear();

  size_t Size() const;
  size_t Capacity() const;

 private:
  struct CachedPage {
    std::shared_ptr<const std::vector<uint8_t>> data;
    int64_t cached_mtime_ms = 0;
  };

  void EvictUntilFit(size_t required_bytes);
  void RemoveLocked(PageId page_id);

  size_t max_size_bytes_;
  size_t current_size_bytes_{0};
  std::list<PageId> lru_order_;  // front = MRU, back = LRU
  std::unordered_map<PageId, std::pair<CachedPage, std::list<PageId>::iterator>>
      pages_;
  mutable std::shared_mutex mu_;
};

}  // namespace fluxcache
