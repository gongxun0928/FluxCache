#include "worker/storage/memory_tier.h"
#include "common/status.h"
#include <cstring>

namespace fluxcache {

MemoryTier::MemoryTier(size_t capacity_limit) : capacity_limit_(capacity_limit) {}

Status MemoryTier::Allocate(size_t size, TierBlockHandle* handle) {
  if (size == 0) {
    return Status::InvalidArgument("allocate size must be positive");
  }
  std::lock_guard<std::mutex> lock(mu_);
  if (used_ + size > capacity_limit_) {
    return Status::ResourceExhausted("memory tier capacity exhausted");
  }

  uint64_t id = next_id_++;
  blocks_[id] = std::string(size, '\0');
  used_ += size;
  handle->id = id;
  return Status::OK();
}

Status MemoryTier::Write(const TierBlockHandle& handle, size_t offset,
                        std::string_view data) {
  if (!handle.valid()) {
    return Status::InvalidArgument("invalid handle");
  }
  std::lock_guard<std::mutex> lock(mu_);
  auto it = blocks_.find(handle.id);
  if (it == blocks_.end()) {
    return Status::InvalidArgument("handle not found");
  }
  std::string& block = it->second;
  if (offset + data.size() > block.size()) {
    return Status::InvalidArgument("write exceeds block size");
  }
  std::memcpy(block.data() + offset, data.data(), data.size());
  return Status::OK();
}

Status MemoryTier::Read(const TierBlockHandle& handle, size_t offset,
                        size_t size, std::string* out) {
  if (!out) {
    return Status::InvalidArgument("null output");
  }
  if (!handle.valid()) {
    return Status::InvalidArgument("invalid handle");
  }
  std::lock_guard<std::mutex> lock(mu_);
  auto it = blocks_.find(handle.id);
  if (it == blocks_.end()) {
    return Status::InvalidArgument("handle not found");
  }
  const std::string& block = it->second;
  if (offset + size > block.size()) {
    return Status::InvalidArgument("read exceeds block size");
  }
  out->assign(block.data() + offset, size);
  return Status::OK();
}

Status MemoryTier::Release(const TierBlockHandle& handle) {
  if (!handle.valid()) {
    return Status::InvalidArgument("invalid handle");
  }
  std::lock_guard<std::mutex> lock(mu_);
  auto it = blocks_.find(handle.id);
  if (it == blocks_.end()) {
    return Status::InvalidArgument("handle not found");
  }
  used_ -= it->second.size();
  blocks_.erase(it);
  return Status::OK();
}

}  // namespace fluxcache
