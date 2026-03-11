#include "worker/page/page_store.h"
#include "worker/meta/meta_store.h"
#include "worker/storage/tier_manager.h"

#include <algorithm>

namespace fluxcache {

PageStore::PageStore(StorageTier* tier, size_t page_size, MetaStore* meta_store)
    : tier_(tier), meta_store_(meta_store), page_size_(page_size) {
  if (page_size_ == 0) page_size_ = 1024 * 1024;
}

void PageStore::SyncMetaPut(PageId id, const PageEntry& entry) {
  if (!meta_store_) return;
  TierType tt;
  uint64_t tier_block_id;
  if (!tier_->GetBlockTierInfo(entry.handle.id, &tt, &tier_block_id)) return;
  PageMeta meta;
  meta.tier_type = tt;
  meta.tier_block_id = tier_block_id;
  meta.cached_mtime_ms = entry.mtime_ms;
  meta_store_->Put(id, meta);
}

void PageStore::SyncMetaDelete(PageId id) {
  if (meta_store_) meta_store_->Delete(id);
}

Status PageStore::GetPage(PageId id, int64_t expected_mtime_ms,
                         std::string* out) {
  if (!out) return Status::InvalidArgument("null output");

  auto it = page_index_.find(id);
  if (it == page_index_.end()) {
    return Status::NotFound("page not found");
  }

  const PageEntry& entry = it->second;
  if (entry.mtime_ms != expected_mtime_ms) {
    // Mtime mismatch: delete stale page and return miss.
    Status s = DeletePage(id);
    if (!s.ok()) return s;
    return Status::NotFound("mtime mismatch, stale page removed");
  }

  return tier_->Read(entry.handle, 0, page_size_, out);
}

Status PageStore::PutPage(PageId id, std::string_view data, int64_t mtime_ms) {
  if (id.block_id == kInvalidBlockId) {
    return Status::InvalidArgument("invalid page id");
  }

  size_t size = std::min(data.size(), page_size_);
  if (size == 0) {
    return Status::InvalidArgument("put page data must be non-empty");
  }

  // If page exists, delete it first to reclaim capacity.
  auto it = page_index_.find(id);
  if (it != page_index_.end()) {
    Status s = tier_->Release(it->second.handle);
    if (!s.ok()) return s;
    RemoveFromBlockIndex(id);
    page_index_.erase(it);
  }

  TierBlockHandle handle;
  Status s = tier_->Allocate(page_size_, &handle);
  if (!s.ok()) return s;

  s = tier_->Write(handle, 0, data.substr(0, size));
  if (!s.ok()) {
    tier_->Release(handle);
    return s;
  }

  page_index_[id] = PageEntry{handle, mtime_ms};
  block_to_pages_[id.block_id].insert(id.page_index);
  SyncMetaPut(id, page_index_[id]);
  return Status::OK();
}

Status PageStore::DeletePage(PageId id) {
  auto it = page_index_.find(id);
  if (it == page_index_.end()) {
    SyncMetaDelete(id);  // Idempotent: ensure MetaStore clean
    return Status::OK();
  }

  Status s = tier_->Release(it->second.handle);
  if (!s.ok()) return s;
  RemoveFromBlockIndex(id);
  page_index_.erase(it);
  SyncMetaDelete(id);
  return Status::OK();
}

Status PageStore::DeleteBlockPages(BlockId block_id) {
  auto it = block_to_pages_.find(block_id);
  if (it == block_to_pages_.end()) {
    if (meta_store_) meta_store_->DeleteByBlock(block_id);
    return Status::OK();  // Idempotent: no pages for this block
  }

  std::set<uint16_t> indices = it->second;
  block_to_pages_.erase(it);
  for (uint16_t page_index : indices) {
    PageId id{block_id, page_index};
    Status s = DeletePage(id);
    if (!s.ok()) return s;
  }
  if (meta_store_) meta_store_->DeleteByBlock(block_id);
  return Status::OK();
}

bool PageStore::Contains(PageId id) const {
  return page_index_.find(id) != page_index_.end();
}

void PageStore::RecoverFromMetaStore() {
  if (!meta_store_ || !meta_store_->is_open()) return;
  auto* tm = dynamic_cast<TierManager*>(tier_);
  if (!tm) return;

  meta_store_->ScanAll([this, tm](PageId id, const PageMeta& meta) {
    TierBlockHandle handle = tm->RegisterRecoveredBlock(meta.tier_type,
                                                        meta.tier_block_id);
    if (handle.valid()) {
      page_index_[id] = PageEntry{handle, meta.cached_mtime_ms};
      block_to_pages_[id.block_id].insert(id.page_index);
    } else {
      meta_store_->Delete(id);  // Clean orphan: tier file missing
    }
  });
}

void PageStore::RemoveFromBlockIndex(PageId id) {
  auto it = block_to_pages_.find(id.block_id);
  if (it == block_to_pages_.end()) return;
  it->second.erase(id.page_index);
  if (it->second.empty()) {
    block_to_pages_.erase(it);
  }
}

}  // namespace fluxcache
