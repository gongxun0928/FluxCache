#pragma once

#include "common/status.h"
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
};

}  // namespace fluxcache
