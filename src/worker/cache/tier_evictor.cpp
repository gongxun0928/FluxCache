#include "worker/cache/tier_evictor.h"
#include "common/metrics/metrics_registry.h"
#include "worker/cache/eviction_policy.h"
#include "worker/meta/meta_store.h"
#include "worker/page/page_store.h"
#include "worker/storage/tier_manager.h"

namespace fluxcache {

TierEvictor::TierEvictor(PageStore* page_store, TierManager* tier_manager,
                         MetaStore* meta_store,
                         EvictionPolicy* eviction_policy,
                         double high_watermark, MetricsRegistry* metrics)
    : page_store_(page_store),
      tier_manager_(tier_manager),
      meta_store_(meta_store),
      eviction_policy_(eviction_policy),
      high_watermark_(high_watermark),
      metrics_(metrics) {}

Status TierEvictor::EvictOne() {
  size_t used = tier_manager_->UsedCapacity();
  size_t limit = tier_manager_->CapacityLimit();
  if (limit == 0) return Status::OK();
  if (static_cast<double>(used) / static_cast<double>(limit) < high_watermark_) {
    return Status::OK();
  }

  auto victim = eviction_policy_->PickVictim();
  if (!victim.has_value()) {
    return Status::NotFound("no victim to evict");
  }

  auto tier_opt = page_store_->GetPageTier(*victim);
  if (!tier_opt.has_value()) {
    return Status::NotFound("victim page not found");
  }

  TierType current = *tier_opt;

  // Demote: Memory -> SSD, SSD -> HDD. Evict if target full or HDD.
  if (current == TierType::kMemory) {
    StorageTier* ssd = tier_manager_->GetTier(TierType::kSSD);
    if (ssd && ssd->UsedCapacity() < ssd->CapacityLimit()) {
      Status s = page_store_->RelocatePage(*victim, TierType::kSSD);
      if (s.ok() && metrics_) {
        metrics_->IncCounter("fluxcache_evictions_total", "type", "demotion");
      }
      return s;
    }
    Status s = page_store_->DeletePage(*victim);
    if (s.ok() && metrics_) {
      metrics_->IncCounter("fluxcache_evictions_total", "type", "eviction");
    }
    return s;
  }

  if (current == TierType::kSSD) {
    StorageTier* hdd = tier_manager_->GetTier(TierType::kHDD);
    if (hdd && hdd->UsedCapacity() < hdd->CapacityLimit()) {
      Status s = page_store_->RelocatePage(*victim, TierType::kHDD);
      if (s.ok() && metrics_) {
        metrics_->IncCounter("fluxcache_evictions_total", "type", "demotion");
      }
      return s;
    }
    Status s = page_store_->DeletePage(*victim);
    if (s.ok() && metrics_) {
      metrics_->IncCounter("fluxcache_evictions_total", "type", "eviction");
    }
    return s;
  }

  // HDD: evict
  Status s = page_store_->DeletePage(*victim);
  if (s.ok() && metrics_) {
    metrics_->IncCounter("fluxcache_evictions_total", "type", "eviction");
  }
  return s;
}

}  // namespace fluxcache
