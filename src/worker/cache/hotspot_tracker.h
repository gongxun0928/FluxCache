#pragma once

#include "common/types.h"
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fluxcache {

/// Entry for a hot page with access count.
struct HotPageEntry {
  PageId page_id;
  uint64_t access_count = 0;
};

/// Tracks page access frequency and provides Top-K hot pages.
/// Thread-safe. Used for /debug/hot-pages and Prometheus metrics.
class HotspotTracker {
 public:
  /// @param top_k_size Maximum number of pages to consider for Top-K. Default 100.
  explicit HotspotTracker(size_t top_k_size = 100);

  /// Record a page access.
  void Record(PageId page_id);

  /// Get top K pages by access count, descending. Returns at most top_k_size.
  std::vector<HotPageEntry> GetTopK(size_t k) const;

  /// Export Prometheus fragment for fluxcache_hot_page_access_total and
  /// fluxcache_hot_pages_tracked.
  std::string ExportPrometheusFragment() const;

  /// Human-readable format for /debug/hot-pages.
  std::string FormatDebug(size_t top_k = 50) const;

 private:
  size_t top_k_size_;
  mutable std::mutex mu_;
  std::unordered_map<PageId, uint64_t> counts_;
  std::atomic<uint64_t> total_accesses_{0};
};

}  // namespace fluxcache
