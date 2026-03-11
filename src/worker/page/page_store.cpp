#include "worker/page/page_store.h"

#include <algorithm>

namespace fluxcache {

PageStore::PageStore(StorageTier* tier, size_t page_size)
    : tier_(tier), page_size_(page_size) {
  if (page_size_ == 0) page_size_ = 1024 * 1024;
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
  return Status::OK();
}

Status PageStore::DeletePage(PageId id) {
  auto it = page_index_.find(id);
  if (it == page_index_.end()) {
    return Status::OK();  // Idempotent: already deleted
  }

  Status s = tier_->Release(it->second.handle);
  if (!s.ok()) return s;
  RemoveFromBlockIndex(id);
  page_index_.erase(it);
  return Status::OK();
}

Status PageStore::DeleteBlockPages(BlockId block_id) {
  auto it = block_to_pages_.find(block_id);
  if (it == block_to_pages_.end()) {
    return Status::OK();  // Idempotent: no pages for this block
  }

  std::set<uint16_t> indices = it->second;
  block_to_pages_.erase(it);
  for (uint16_t page_index : indices) {
    PageId id{block_id, page_index};
    Status s = DeletePage(id);
    if (!s.ok()) return s;
  }
  return Status::OK();
}

bool PageStore::Contains(PageId id) const {
  return page_index_.find(id) != page_index_.end();
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
