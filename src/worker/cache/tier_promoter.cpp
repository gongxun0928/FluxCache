#include "worker/cache/tier_promoter.h"
#include "worker/cache/eviction_policy.h"
#include "worker/meta/meta_store.h"
#include "worker/page/page_store.h"
#include "worker/storage/tier_manager.h"

namespace fluxcache {

TierPromoter::TierPromoter(PageStore* page_store, TierManager* tier_manager,
                           MetaStore* meta_store,
                           EvictionPolicy* eviction_policy)
    : page_store_(page_store),
      tier_manager_(tier_manager),
      meta_store_(meta_store),
      eviction_policy_(eviction_policy) {}

Status TierPromoter::PromoteOne() {
  if (!tier_manager_->GetTier(TierType::kMemory)) {
    return Status::InvalidArgument("Memory tier not found");
  }

  std::vector<PageId> ordered = eviction_policy_->GetOrderedFromMru();
  for (const PageId& id : ordered) {
    auto tier_opt = page_store_->GetPageTier(id);
    if (!tier_opt.has_value()) continue;

    TierType tt = *tier_opt;
    if (tt != TierType::kSSD && tt != TierType::kHDD) continue;

    return page_store_->RelocatePage(id, TierType::kMemory);
  }

  return Status::NotFound("no promotion candidate in slow tier");
}

}  // namespace fluxcache
