#pragma once

#include "common/status.h"
#include "common/types.h"

namespace fluxcache {

class EvictionPolicy;
class MetaStore;
class PageStore;
class TierManager;

// Evicts or demotes cold pages when capacity exceeds high watermark.
// Uses EvictionPolicy::PickVictim for LRU victim selection.
class TierEvictor {
 public:
  TierEvictor(PageStore* page_store, TierManager* tier_manager,
              MetaStore* meta_store, EvictionPolicy* eviction_policy,
              double high_watermark = 0.9);

  // When used/total >= high_watermark, evicts or demotes one victim.
  // Returns OK on success, NotFound if nothing to evict, or other error.
  Status EvictOne();

 private:
  PageStore* page_store_;
  TierManager* tier_manager_;
  MetaStore* meta_store_;
  EvictionPolicy* eviction_policy_;
  double high_watermark_;
};

}  // namespace fluxcache
