#pragma once

#include "common/status.h"
#include "common/types.h"
#include "worker/storage/storage_tier.h"

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fluxcache {

// Page-level cache engine. Maintains PageId -> TierBlockHandle index and
// BlockId -> page_index set secondary index. Validates mtime on GetPage.
class PageStore {
 public:
  explicit PageStore(StorageTier* tier, size_t page_size = 1024 * 1024);

  // GetPage: returns data if hit and expected_mtime matches; on mismatch
  // deletes the stale page and returns NotFound.
  Status GetPage(PageId id, int64_t expected_mtime_ms, std::string* out);

  // PutPage: writes page with mtime; returns ResourceExhausted when tier
  // capacity is insufficient.
  Status PutPage(PageId id, std::string_view data, int64_t mtime_ms);

  Status DeletePage(PageId id);
  Status DeleteBlockPages(BlockId block_id);
  bool Contains(PageId id) const;

 private:
  struct PageEntry {
    TierBlockHandle handle;
    int64_t mtime_ms{0};
  };

  void RemoveFromBlockIndex(PageId id);

  StorageTier* tier_;
  size_t page_size_;
  std::unordered_map<PageId, PageEntry> page_index_;
  std::unordered_map<BlockId, std::set<uint16_t>> block_to_pages_;
};

}  // namespace fluxcache
