#pragma once

#include "worker/storage/storage_tier.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace fluxcache {

// Manages multiple StorageTiers with priority-based allocation.
// Tiers are tried in order; the first with capacity succeeds.
// Handles are mapped internally to the owning tier.
class TierManager : public StorageTier {
 public:
  TierManager() = default;

  // Adds a tier. Order defines priority: first added has highest priority.
  void AddTier(std::unique_ptr<StorageTier> tier);

  Status Allocate(size_t size, TierBlockHandle* handle) override;
  Status Write(const TierBlockHandle& handle, size_t offset,
               std::string_view data) override;
  Status Read(const TierBlockHandle& handle, size_t offset, size_t size,
              std::string* out) override;
  Status Release(const TierBlockHandle& handle) override;

  size_t UsedCapacity() const override;
  size_t CapacityLimit() const override;

  TierType GetTierType() const override {
    return TierType::kMemory;  // Composite; per-handle via GetBlockTierInfo.
  }

  bool GetBlockTierInfo(uint64_t handle_id, TierType* out_type,
                        uint64_t* out_tier_block_id) const override;

  // Registers an existing tier block for recovery. Returns invalid handle on
  // failure (tier not found or block missing).
  TierBlockHandle RegisterRecoveredBlock(TierType tier_type,
                                        uint64_t tier_block_id);

  // Allocates in a specific tier. Returns ResourceExhausted if that tier is full.
  Status AllocateInTier(TierType tier_type, size_t size,
                       TierBlockHandle* handle);

  // Returns the StorageTier for the given type, or nullptr if not found.
  StorageTier* GetTier(TierType tier_type) const;

 private:
  struct HandleMapping {
    StorageTier* tier{nullptr};
    TierBlockHandle tier_handle;
  };

  std::vector<std::unique_ptr<StorageTier>> tiers_;
  std::unordered_map<uint64_t, HandleMapping> handle_to_tier_;
  uint64_t next_global_id_{1};
};

}  // namespace fluxcache
