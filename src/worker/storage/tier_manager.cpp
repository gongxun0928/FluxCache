#include "worker/storage/tier_manager.h"
#include "common/status.h"

namespace fluxcache {

void TierManager::AddTier(std::unique_ptr<StorageTier> tier) {
  if (tier) {
    tiers_.push_back(std::move(tier));
  }
}

Status TierManager::Allocate(size_t size, TierBlockHandle* handle) {
  if (!handle) {
    return Status::InvalidArgument("null handle");
  }
  for (auto& tier : tiers_) {
    TierBlockHandle tier_handle;
    Status s = tier->Allocate(size, &tier_handle);
    if (s.ok()) {
      uint64_t global_id = next_global_id_++;
      handle_to_tier_[global_id] = {tier.get(), tier_handle};
      handle->id = global_id;
      return Status::OK();
    }
  }
  return Status::ResourceExhausted("all tiers exhausted");
}

Status TierManager::Write(const TierBlockHandle& handle, size_t offset,
                          std::string_view data) {
  if (!handle.valid()) {
    return Status::InvalidArgument("invalid handle");
  }
  auto it = handle_to_tier_.find(handle.id);
  if (it == handle_to_tier_.end()) {
    return Status::InvalidArgument("handle not found");
  }
  return it->second.tier->Write(it->second.tier_handle, offset, data);
}

Status TierManager::Read(const TierBlockHandle& handle, size_t offset,
                         size_t size, std::string* out) {
  if (!handle.valid()) {
    return Status::InvalidArgument("invalid handle");
  }
  auto it = handle_to_tier_.find(handle.id);
  if (it == handle_to_tier_.end()) {
    return Status::InvalidArgument("handle not found");
  }
  return it->second.tier->Read(it->second.tier_handle, offset, size, out);
}

Status TierManager::Release(const TierBlockHandle& handle) {
  if (!handle.valid()) {
    return Status::InvalidArgument("invalid handle");
  }
  auto it = handle_to_tier_.find(handle.id);
  if (it == handle_to_tier_.end()) {
    return Status::InvalidArgument("handle not found");
  }
  Status s = it->second.tier->Release(it->second.tier_handle);
  handle_to_tier_.erase(it);
  return s;
}

size_t TierManager::UsedCapacity() const {
  size_t total = 0;
  for (const auto& tier : tiers_) {
    total += tier->UsedCapacity();
  }
  return total;
}

size_t TierManager::CapacityLimit() const {
  size_t total = 0;
  for (const auto& tier : tiers_) {
    total += tier->CapacityLimit();
  }
  return total;
}

}  // namespace fluxcache
