#pragma once

#include "worker/storage/storage_tier.h"
#include <cstddef>
#include <string>
#include <unordered_map>

namespace fluxcache {

// In-memory implementation of StorageTier with a fixed capacity limit.
class MemoryTier : public StorageTier {
 public:
  explicit MemoryTier(size_t capacity_limit);

  Status Allocate(size_t size, TierBlockHandle* handle) override;
  Status Write(const TierBlockHandle& handle, size_t offset,
               std::string_view data) override;
  Status Read(const TierBlockHandle& handle, size_t offset, size_t size,
              std::string* out) override;
  Status Release(const TierBlockHandle& handle) override;

  size_t UsedCapacity() const override { return used_; }
  size_t CapacityLimit() const override { return capacity_limit_; }

  TierType GetTierType() const override { return TierType::kMemory; }

 private:
  size_t capacity_limit_;
  size_t used_{0};
  uint64_t next_id_{1};
  std::unordered_map<uint64_t, std::string> blocks_;
};

}  // namespace fluxcache
