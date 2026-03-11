#include <filesystem>
#include <gtest/gtest.h>

#include "common/status.h"
#include "common/types.h"
#include "worker/cache/eviction_policy_factory.h"
#include "worker/cache/tier_evictor.h"
#include "worker/cache/tier_promoter.h"
#include "worker/meta/meta_store.h"
#include "worker/page/page_store.h"
#include "worker/storage/memory_tier.h"
#include "worker/storage/ssd_tier.h"
#include "worker/storage/tier_manager.h"

namespace fluxcache {

namespace fs = std::filesystem;

class TierPromotionTest : public ::testing::Test {
 protected:
  static constexpr size_t kPageSize = 1024;

  void SetUp() override {
    tmp_dir_ = fs::temp_directory_path() / "fluxcache_tier_promotion_test";
    fs::create_directories(tmp_dir_);
    ssd_path_ = (tmp_dir_ / "ssd").string();
    metastore_path_ = (tmp_dir_ / "metastore").string();
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(tmp_dir_, ec);
  }

  fs::path tmp_dir_;
  std::string ssd_path_;
  std::string metastore_path_;
};

TEST_F(TierPromotionTest, PromoteOne_HotPageInSsd_MovesToMemory) {
  // Memory: 2 pages, SSD: 10 pages. First 2 in Memory, 3rd in SSD.
  // EvictOne demotes one from Memory to SSD, freeing a slot. Then PromoteOne.
  TierManager tier_mgr;
  tier_mgr.AddTier(std::make_unique<MemoryTier>(2 * kPageSize));
  tier_mgr.AddTier(std::make_unique<SsdTier>(ssd_path_, 10 * kPageSize));

  MetaStore meta;
  ASSERT_TRUE(meta.Open(metastore_path_));

  auto policy = CreateEvictionPolicy(EvictionPolicyType::kLRU);
  PageStore store(&tier_mgr, kPageSize, &meta, policy.get());

  const int64_t mtime = 1000;
  PageId p1{MakeBlockId(1, 0), 0};
  PageId p2{MakeBlockId(2, 0), 0};
  PageId p3{MakeBlockId(3, 0), 0};
  std::string data(kPageSize, 'x');

  ASSERT_TRUE(store.PutPage(p1, data, mtime).ok());
  ASSERT_TRUE(store.PutPage(p2, data, mtime).ok());
  ASSERT_TRUE(store.PutPage(p3, data, mtime).ok());

  EXPECT_EQ(store.GetPageTier(p1), TierType::kMemory);
  EXPECT_EQ(store.GetPageTier(p2), TierType::kMemory);
  EXPECT_EQ(store.GetPageTier(p3), TierType::kSSD);

  // Free a Memory slot by demoting (used=3, limit=12, over 0.2 threshold)
  TierEvictor evictor(&store, &tier_mgr, &meta, policy.get(), 0.2);
  auto ev_s = evictor.EvictOne();
  ASSERT_TRUE(ev_s.ok()) << ev_s.message();

  TierPromoter promoter(&store, &tier_mgr, &meta, policy.get());
  auto s = promoter.PromoteOne();
  ASSERT_TRUE(s.ok()) << s.message();

  EXPECT_EQ(store.GetPageTier(p3), TierType::kMemory);

  std::string out;
  ASSERT_TRUE(store.GetPage(p3, mtime, &out).ok());
  EXPECT_EQ(out, data);
}

TEST_F(TierPromotionTest, PromoteOne_AllInMemory_ReturnsNotFound) {
  TierManager tier_mgr;
  tier_mgr.AddTier(std::make_unique<MemoryTier>(10 * kPageSize));
  tier_mgr.AddTier(std::make_unique<SsdTier>(ssd_path_, 10 * kPageSize));

  MetaStore meta;
  ASSERT_TRUE(meta.Open(metastore_path_));

  auto policy = CreateEvictionPolicy(EvictionPolicyType::kLRU);
  PageStore store(&tier_mgr, kPageSize, &meta, policy.get());

  PageId p1{MakeBlockId(1, 0), 0};
  store.PutPage(p1, std::string(kPageSize, 'a'), 2000);

  TierPromoter promoter(&store, &tier_mgr, &meta, policy.get());
  auto s = promoter.PromoteOne();
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kNotFound);
}

TEST_F(TierPromotionTest, EvictOne_OverThreshold_DemotesMemoryToSsd) {
  // Memory: 1 page, SSD: 10 pages. high_watermark 0.5.
  // Put 2 pages -> 1 in Memory, 1 in SSD. used=2, limit=11, ratio=0.18 < 0.5.
  // Need more: put 6 pages. 1 in Memory, 6 in SSD (Memory full). used=7, limit=11, 7/11=0.63 > 0.5.
  TierManager tier_mgr;
  tier_mgr.AddTier(std::make_unique<MemoryTier>(1 * kPageSize));
  tier_mgr.AddTier(std::make_unique<SsdTier>(ssd_path_, 10 * kPageSize));

  MetaStore meta;
  ASSERT_TRUE(meta.Open(metastore_path_));

  auto policy = CreateEvictionPolicy(EvictionPolicyType::kLRU);
  PageStore store(&tier_mgr, kPageSize, &meta, policy.get());

  const int64_t mtime = 3000;
  std::string data(kPageSize, 'b');
  for (int i = 0; i < 7; ++i) {
    PageId id{MakeBlockId(1, 0), static_cast<uint16_t>(i)};
    ASSERT_TRUE(store.PutPage(id, data, mtime).ok());
  }

  TierEvictor evictor(&store, &tier_mgr, &meta, policy.get(), 0.5);
  auto s = evictor.EvictOne();
  ASSERT_TRUE(s.ok()) << s.message();

  // One page should have been demoted or evicted. LRU order = first inserted.
  // After eviction, one of the Memory pages (the LRU) should be demoted to SSD.
  size_t in_memory = 0;
  for (int i = 0; i < 7; ++i) {
    PageId id{MakeBlockId(1, 0), static_cast<uint16_t>(i)};
    if (store.Contains(id)) {
      auto tt = store.GetPageTier(id);
      if (tt == TierType::kMemory) ++in_memory;
    }
  }
  EXPECT_LE(in_memory, 1u) << "At most 1 page should remain in Memory";
}

TEST_F(TierPromotionTest, EvictOne_UnderThreshold_DoesNothing) {
  TierManager tier_mgr;
  tier_mgr.AddTier(std::make_unique<MemoryTier>(5 * kPageSize));
  tier_mgr.AddTier(std::make_unique<SsdTier>(ssd_path_, 10 * kPageSize));

  MetaStore meta;
  ASSERT_TRUE(meta.Open(metastore_path_));

  auto policy = CreateEvictionPolicy(EvictionPolicyType::kLRU);
  PageStore store(&tier_mgr, kPageSize, &meta, policy.get());

  PageId p1{MakeBlockId(1, 0), 0};
  store.PutPage(p1, std::string(kPageSize, 'c'), 4000);

  TierEvictor evictor(&store, &tier_mgr, &meta, policy.get(), 0.99);
  auto s = evictor.EvictOne();
  ASSERT_TRUE(s.ok()) << s.message();

  EXPECT_TRUE(store.Contains(p1));
}

TEST_F(TierPromotionTest, RelocatePage_IndexConsistency) {
  // Memory: 2 pages, SSD: 10. Put 2 fillers in Memory, id in SSD.
  // EvictOne demotes one filler to SSD, freeing Memory. Relocate id to Memory.
  TierManager tier_mgr;
  tier_mgr.AddTier(std::make_unique<MemoryTier>(2 * kPageSize));
  tier_mgr.AddTier(std::make_unique<SsdTier>(ssd_path_, 10 * kPageSize));

  MetaStore meta;
  ASSERT_TRUE(meta.Open(metastore_path_));

  auto policy = CreateEvictionPolicy(EvictionPolicyType::kLRU);
  PageStore store(&tier_mgr, kPageSize, &meta, policy.get());

  PageId filler1{MakeBlockId(6, 0), 0};
  PageId filler2{MakeBlockId(7, 0), 0};
  PageId id{MakeBlockId(5, 0), 0};
  std::string data(kPageSize, 'd');

  store.PutPage(filler1, std::string(kPageSize, 'e'), 5001);
  store.PutPage(filler2, std::string(kPageSize, 'f'), 5002);
  ASSERT_TRUE(store.PutPage(id, data, 5000).ok());

  ASSERT_EQ(store.GetPageTier(filler1), TierType::kMemory);
  ASSERT_EQ(store.GetPageTier(filler2), TierType::kMemory);
  ASSERT_EQ(store.GetPageTier(id), TierType::kSSD);

  TierEvictor evictor(&store, &tier_mgr, &meta, policy.get(), 0.1);
  ASSERT_TRUE(evictor.EvictOne().ok());

  auto meta_before = meta.Get(id);
  ASSERT_TRUE(meta_before.has_value());
  EXPECT_EQ(meta_before->tier_type, TierType::kSSD);

  auto s = store.RelocatePage(id, TierType::kMemory);
  ASSERT_TRUE(s.ok()) << s.message();

  EXPECT_EQ(store.GetPageTier(id), TierType::kMemory);
  auto meta_after = meta.Get(id);
  ASSERT_TRUE(meta_after.has_value());
  EXPECT_EQ(meta_after->tier_type, TierType::kMemory);

  std::string out;
  ASSERT_TRUE(store.GetPage(id, 5000, &out).ok());
  EXPECT_EQ(out, data);
}

}  // namespace fluxcache
