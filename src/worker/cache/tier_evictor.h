#pragma once

#include "common/status.h"
#include "common/types.h"
#include <functional>
#include <string>

namespace fluxcache {

class EvictionPolicy;
class MetaStore;
class MetricsRegistry;
class PageStore;
class TierManager;

// Callback invoked before evicting a page. If it returns non-OK, eviction is
// skipped (page is not deleted).
using BeforeEvictCallback =
    std::function<Status(PageId page_id, const std::string& page_data)>;

// Evicts or demotes cold pages when capacity exceeds high watermark.
// Uses EvictionPolicy::PickVictim for LRU victim selection.
class TierEvictor {
 public:
  TierEvictor(PageStore* page_store, TierManager* tier_manager,
              MetaStore* meta_store, EvictionPolicy* eviction_policy,
              double high_watermark = 0.9,
              MetricsRegistry* metrics = nullptr);

  void SetBeforeEvictCallback(BeforeEvictCallback cb) {
    before_evict_cb_ = std::move(cb);
  }

  // When used/total >= high_watermark, evicts or demotes one victim.
  // Returns OK on success, NotFound if nothing to evict, or other error.
  Status EvictOne();

 private:
  PageStore* page_store_;
  TierManager* tier_manager_;
  MetaStore* meta_store_;
  EvictionPolicy* eviction_policy_;
  double high_watermark_;
  MetricsRegistry* metrics_;
  BeforeEvictCallback before_evict_cb_;
};

}  // namespace fluxcache
