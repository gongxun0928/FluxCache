#include "common/types.h"
#include "worker/cache/hotspot_tracker.h"
#include <gtest/gtest.h>

namespace fluxcache {

TEST(HotspotTrackerTest, RecordAndGetTopK) {
  HotspotTracker tracker(10);

  tracker.Record(MakePageId(MakeBlockId(1, 0), 0));
  tracker.Record(MakePageId(MakeBlockId(1, 0), 0));
  tracker.Record(MakePageId(MakeBlockId(1, 0), 0));
  tracker.Record(MakePageId(MakeBlockId(2, 0), 1));
  tracker.Record(MakePageId(MakeBlockId(2, 0), 1));

  auto top = tracker.GetTopK(5);
  ASSERT_EQ(top.size(), 2u);
  EXPECT_EQ(top[0].page_id.block_id, MakeBlockId(1, 0));
  EXPECT_EQ(top[0].page_id.page_index, 0);
  EXPECT_EQ(top[0].access_count, 3u);
  EXPECT_EQ(top[1].page_id.block_id, MakeBlockId(2, 0));
  EXPECT_EQ(top[1].page_id.page_index, 1);
  EXPECT_EQ(top[1].access_count, 2u);
}

TEST(HotspotTrackerTest, GetTopKReturnsAtMostK) {
  HotspotTracker tracker(10);

  for (int i = 0; i < 5; ++i) {
    tracker.Record(MakePageId(MakeBlockId(1, i), static_cast<uint16_t>(i)));
  }

  auto top = tracker.GetTopK(3);
  EXPECT_EQ(top.size(), 3u);
}

TEST(HotspotTrackerTest, ExportPrometheusFragmentContainsMetrics) {
  HotspotTracker tracker(10);
  tracker.Record(MakePageId(MakeBlockId(1, 0), 0));

  std::string frag = tracker.ExportPrometheusFragment();
  EXPECT_TRUE(frag.find("fluxcache_hot_page_access_total") != std::string::npos);
  EXPECT_TRUE(frag.find("fluxcache_hot_pages_tracked") != std::string::npos);
}

}  // namespace fluxcache
