#include "common/metrics/slow_request_tracker.h"
#include <gtest/gtest.h>
#include <string>

namespace fluxcache {

TEST(SlowRequestTrackerTest, RecordsOnlyWhenAboveThreshold) {
  SlowRequestTracker tracker(0.5);  // 500ms threshold

  tracker.Record("worker", "ReadPages", 0.3, "");
  auto recent = tracker.GetRecentSlowRequests(10);
  EXPECT_EQ(recent.size(), 0u) << "0.3s should not be recorded";

  tracker.Record("worker", "ReadPages", 0.6, "");
  recent = tracker.GetRecentSlowRequests(10);
  EXPECT_EQ(recent.size(), 1u);
  EXPECT_EQ(recent[0].service, "worker");
  EXPECT_EQ(recent[0].method, "ReadPages");
  EXPECT_GE(recent[0].duration_sec, 0.59);
  EXPECT_LE(recent[0].duration_sec, 0.61);
}

TEST(SlowRequestTrackerTest, GetRecentReturnsNewestFirst) {
  SlowRequestTracker tracker(0.1);

  tracker.Record("worker", "ReadPages", 0.5, "block=1");
  tracker.Record("master", "GetFileInfo", 0.2, "");
  tracker.Record("worker", "BatchReadPages", 0.3, "");

  auto recent = tracker.GetRecentSlowRequests(10);
  ASSERT_EQ(recent.size(), 3u);
  EXPECT_EQ(recent[0].method, "BatchReadPages");
  EXPECT_EQ(recent[1].method, "GetFileInfo");
  EXPECT_EQ(recent[2].method, "ReadPages");
}

TEST(SlowRequestTrackerTest, ExportPrometheusFragmentContainsMetrics) {
  SlowRequestTracker tracker(0.1);
  tracker.Record("worker", "ReadPages", 1.5, "");

  std::string frag = tracker.ExportPrometheusFragment();
  EXPECT_TRUE(frag.find("fluxcache_slow_requests_total") != std::string::npos);
  EXPECT_TRUE(frag.find("fluxcache_slow_request_duration_seconds") !=
              std::string::npos);
}

TEST(SlowRequestTrackerTest, RingBufferLimitsSize) {
  SlowRequestTracker tracker(0.01, 5);  // threshold 10ms, max 5 entries

  for (int i = 0; i < 10; ++i) {
    tracker.Record("worker", "ReadPages", 0.5 + i * 0.01, "");
  }

  auto recent = tracker.GetRecentSlowRequests(20);
  EXPECT_EQ(recent.size(), 5u) << "Should cap at 5 most recent";
}

}  // namespace fluxcache
