#pragma once

#include "common/status.h"
#include "common/types.h"

namespace fluxcache {

class EvictionPolicy;
class MetaStore;
class PageStore;
class TierManager;

// Promotes hot pages from slow tiers (SSD/HDD) to fast tier (Memory).
// Uses EvictionPolicy::GetOrderedFromMru to find the MRU page in a slow tier.
class TierPromoter {
 public:
  TierPromoter(PageStore* page_store, TierManager* tier_manager,
               MetaStore* meta_store, EvictionPolicy* eviction_policy);

  // Tries to promote one page from SSD/HDD to Memory. Returns OK on success,
  // NotFound if no promotion candidate, or other error.
  Status PromoteOne();

 private:
  PageStore* page_store_;
  TierManager* tier_manager_;
  MetaStore* meta_store_;
  EvictionPolicy* eviction_policy_;
};

}  // namespace fluxcache
