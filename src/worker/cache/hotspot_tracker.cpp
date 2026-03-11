#include "worker/cache/hotspot_tracker.h"
#include "common/types.h"
#include <algorithm>
#include <sstream>

namespace fluxcache {

HotspotTracker::HotspotTracker(size_t top_k_size) : top_k_size_(top_k_size) {}

void HotspotTracker::Record(PageId page_id) {
  total_accesses_.fetch_add(1, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(mu_);
  counts_[page_id]++;
}

std::vector<HotPageEntry> HotspotTracker::GetTopK(size_t k) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<HotPageEntry> entries;
  entries.reserve(counts_.size());
  for (const auto& [pid, cnt] : counts_) {
    entries.push_back({pid, cnt});
  }
  std::sort(entries.begin(), entries.end(),
            [](const HotPageEntry& a, const HotPageEntry& b) {
              return a.access_count > b.access_count;
            });
  size_t n = std::min(k, entries.size());
  return std::vector<HotPageEntry>(entries.begin(), entries.begin() + n);
}

std::string HotspotTracker::ExportPrometheusFragment() const {
  std::lock_guard<std::mutex> lock(mu_);
  uint64_t total = total_accesses_.load(std::memory_order_relaxed);
  uint64_t tracked = counts_.size();

  std::ostringstream out;
  out << "# HELP fluxcache_hot_page_access_total Total page access count.\n";
  out << "# TYPE fluxcache_hot_page_access_total counter\n";
  out << "fluxcache_hot_page_access_total " << total << "\n";
  out << "# HELP fluxcache_hot_pages_tracked Number of unique pages tracked.\n";
  out << "# TYPE fluxcache_hot_pages_tracked gauge\n";
  out << "fluxcache_hot_pages_tracked " << tracked << "\n";

  return out.str();
}

std::string HotspotTracker::FormatDebug(size_t top_k) const {
  auto top = GetTopK(top_k);
  std::ostringstream out;
  out << "hot_pages_count=" << top.size() << "\n";
  for (const auto& e : top) {
    out << "block_id=" << e.page_id.block_id
        << " page_index=" << e.page_id.page_index
        << " access_count=" << e.access_count << "\n";
  }
  return out.str();
}

}  // namespace fluxcache
