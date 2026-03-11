#pragma once

#include "common/status.h"
#include "common/types.h"
#include <cstddef>
#include <string>
#include <string_view>

namespace fluxcache {

// Opaque handle for an allocated block in a storage tier.
struct TierBlockHandle {
  uint64_t id{0};

  bool valid() const { return id != 0; }
};

// Abstract interface for page-level block storage (Memory/SSD/HDD tiers).
class StorageTier {
 public:
  virtual ~StorageTier() = default;

  // Allocates a block of `size` bytes. Returns ResourceExhausted when
  // capacity limit would be exceeded, InvalidArgument when size is 0.
  virtual Status Allocate(size_t size, TierBlockHandle* handle) = 0;

  // Writes `data` into the block at `offset`. InvalidArgument if handle
  // invalid or offset + data.size() exceeds block size.
  virtual Status Write(const TierBlockHandle& handle, size_t offset,
                       std::string_view data) = 0;

  // Reads `size` bytes from the block at `offset` into `out`. InvalidArgument
  // if handle invalid or offset + size exceeds block size.
  virtual Status Read(const TierBlockHandle& handle, size_t offset, size_t size,
                     std::string* out) = 0;

  // Releases the block and reclaims capacity.
  virtual Status Release(const TierBlockHandle& handle) = 0;

  virtual size_t UsedCapacity() const = 0;
  virtual size_t CapacityLimit() const = 0;

  // For MetaStore recovery: tier type and block id within this tier.
  virtual TierType GetTierType() const = 0;

  // For MetaStore: get (tier_type, tier_block_id) for a handle. Returns false
  // if handle unknown. Default: single-tier returns (GetTierType(), handle_id).
  virtual bool GetBlockTierInfo(uint64_t handle_id, TierType* out_type,
                                uint64_t* out_tier_block_id) const {
    *out_type = GetTierType();
    *out_tier_block_id = handle_id;
    return true;
  }

  // For recovery: returns true if block exists. MemoryTier returns false.
  virtual bool BlockExists(uint64_t block_id) const { return false; }
};

}  // namespace fluxcache
