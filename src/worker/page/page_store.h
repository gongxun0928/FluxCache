#pragma once

#include "common/status.h"
#include "common/types.h"
#include "worker/storage/storage_tier.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fluxcache {

class EvictionPolicy;
class MetaStore;

// Page-level cache engine. Maintains PageId -> TierBlockHandle index and
// BlockId -> page_index set secondary index. Validates mtime on GetPage.
// Optionally syncs with MetaStore for recovery.
class PageStore {
 public:
  // tier: storage backend. meta_store: optional, for persistence.
  // eviction_policy: optional, for OnInsert/OnAccess/OnRemove callbacks.
  explicit PageStore(StorageTier* tier, size_t page_size = 1024 * 1024,
                    MetaStore* meta_store = nullptr,
                    EvictionPolicy* eviction_policy = nullptr);

  // GetPage: returns data if hit and expected_mtime matches; on mismatch
  // deletes the stale page and returns NotFound, unless allow_keep_on_mismatch.
  Status GetPage(PageId id, int64_t expected_mtime_ms, std::string* out,
                 bool allow_keep_on_mismatch = false);

  // GetPageRelaxed: returns data if page exists, regardless of mtime.
  Status GetPageRelaxed(PageId id, std::string* out);

  // PutPage: writes page with mtime; returns ResourceExhausted when tier
  // capacity is insufficient.
  Status PutPage(PageId id, std::string_view data, int64_t mtime_ms);

  Status DeletePage(PageId id);
  Status DeleteBlockPages(BlockId block_id);
  bool Contains(PageId id) const;
  bool ContainsBlock(BlockId block_id) const;

  // Recovers page_index_ from MetaStore. Requires tier to be TierManager and
  // meta_store set. Cleans orphan MetaStore entries when tier file missing.
  void RecoverFromMetaStore();

  // Returns the tier type for a page, or nullopt if not found.
  std::optional<TierType> GetPageTier(PageId id) const;

  // Relocates a page to the target tier. Requires tier to be TierManager.
  // Does not notify EvictionPolicy (page remains in cache).
  Status RelocatePage(PageId id, TierType target_tier);

  // GC reconciliation: clean orphan and misplaced blocks. Uses paginated scan.
  // Returns (audit_inode_ids, audit_block_ids) for blocks actually cleaned.
  struct GcAudit {
    std::vector<uint64_t> audit_inode_ids;
    std::vector<uint64_t> audit_block_ids;
  };
  GcAudit ReconcileGc(const std::vector<uint64_t>& orphan_inode_ids,
                      const std::vector<uint64_t>& misplaced_block_ids,
                      WorkerId my_worker_id,
                      const std::vector<WorkerId>& ring_worker_ids,
                      size_t batch_limit = 1000);

 private:
  struct PageEntry {
    TierBlockHandle handle;
    int64_t mtime_ms{0};
  };

  void RemoveFromBlockIndex(PageId id);
  void SyncMetaPut(PageId id, const PageEntry& entry);
  void SyncMetaDelete(PageId id);

  static constexpr size_t kNumStripes = 256;

  // Stripe index for BlockId. All pages in same block share one stripe.
  static size_t StripeIndex(BlockId block_id) {
    return static_cast<size_t>(block_id) % kNumStripes;
  }

  // Internal delete without locking. Caller must hold stripe unique lock.
  Status DeletePageUnlocked(PageId id);

  StorageTier* tier_;
  MetaStore* meta_store_;
  EvictionPolicy* eviction_policy_;
  size_t page_size_;
  std::unordered_map<PageId, PageEntry> page_index_;
  std::unordered_map<BlockId, std::set<uint16_t>> block_to_pages_;

  mutable std::shared_mutex recovery_mu_;
  mutable std::array<std::shared_mutex, kNumStripes> stripe_locks_;

  std::mutex gc_mu_;
  std::optional<std::pair<BlockId, uint16_t>> gc_scan_cursor_;
};

}  // namespace fluxcache
