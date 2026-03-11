#pragma once

#include "common/types.h"

#include <optional>
#include <vector>

namespace fluxcache {

enum class EvictionPolicyType : uint8_t {
  kLRU = 0,
  kLFU = 1,  // Reserved for P2-04
};

// Pluggable eviction policy interface. Implementations track page access order
// and provide PickVictim() for cache eviction. Not thread-safe in Phase 1.
class EvictionPolicy {
 public:
  virtual ~EvictionPolicy() = default;

  // Called when a page is inserted (e.g. after PutPage succeeds).
  virtual void OnInsert(PageId id) = 0;

  // Called when a page is accessed (e.g. GetPage hit). Updates eviction order.
  virtual void OnAccess(PageId id) = 0;

  // Called when a page is removed (e.g. DeletePage, DeleteBlockPages).
  virtual void OnRemove(PageId id) = 0;

  // Returns the next page to evict, or nullopt if none.
  virtual std::optional<PageId> PickVictim() = 0;

  // Returns pages ordered from MRU to LRU. Used by TierPromoter for promotion
  // candidate selection. Default returns empty.
  virtual std::vector<PageId> GetOrderedFromMru() const { return {}; }
};

}  // namespace fluxcache
