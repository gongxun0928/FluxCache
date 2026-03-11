#include <filesystem>
#include <gtest/gtest.h>

#include "common/status.h"
#include "common/types.h"
#include "worker/meta/meta_store.h"
#include "worker/page/page_store.h"
#include "worker/storage/memory_tier.h"
#include "worker/storage/ssd_tier.h"
#include "worker/storage/tier_manager.h"

namespace fluxcache {

namespace fs = std::filesystem;

class MetaStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tmp_dir_ = fs::temp_directory_path() / "fluxcache_meta_store_test";
    fs::create_directories(tmp_dir_);
    metastore_path_ = (tmp_dir_ / "metastore").string();
    ssd_path_ = (tmp_dir_ / "ssd").string();
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(tmp_dir_, ec);
  }

  fs::path tmp_dir_;
  std::string metastore_path_;
  std::string ssd_path_;
};

TEST_F(MetaStoreTest, PutGetDelete) {
  MetaStore store;
  ASSERT_TRUE(store.Open(metastore_path_));

  PageId id{MakeBlockId(1, 0), 0};
  PageMeta meta;
  meta.tier_type = TierType::kSSD;
  meta.tier_block_id = 42;
  meta.cached_mtime_ms = 1000;

  ASSERT_TRUE(store.Put(id, meta));
  auto got = store.Get(id);
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->tier_type, TierType::kSSD);
  EXPECT_EQ(got->tier_block_id, 42u);
  EXPECT_EQ(got->cached_mtime_ms, 1000);

  ASSERT_TRUE(store.Delete(id));
  EXPECT_FALSE(store.Get(id).has_value());
}

TEST_F(MetaStoreTest, DeleteByBlock) {
  MetaStore store;
  ASSERT_TRUE(store.Open(metastore_path_));

  BlockId block_id = MakeBlockId(2, 0);
  for (uint16_t i = 0; i < 3; ++i) {
    PageMeta meta;
    meta.tier_type = TierType::kSSD;
    meta.tier_block_id = i;
    meta.cached_mtime_ms = 2000;
    ASSERT_TRUE(store.Put(PageId{block_id, i}, meta));
  }

  ASSERT_TRUE(store.DeleteByBlock(block_id));

  EXPECT_FALSE(store.Get(PageId{block_id, 0}).has_value());
  EXPECT_FALSE(store.Get(PageId{block_id, 1}).has_value());
  EXPECT_FALSE(store.Get(PageId{block_id, 2}).has_value());
}

TEST_F(MetaStoreTest, ScanBlock) {
  MetaStore store;
  ASSERT_TRUE(store.Open(metastore_path_));

  BlockId block_id = MakeBlockId(3, 0);
  store.Put(PageId{block_id, 0}, {TierType::kSSD, 10, 3000});
  store.Put(PageId{block_id, 1}, {TierType::kSSD, 11, 3001});
  store.Put(PageId{MakeBlockId(4, 0), 0}, {TierType::kSSD, 20, 4000});

  std::vector<std::pair<PageId, PageMeta>> found;
  store.ScanBlock(block_id, [&found](PageId id, const PageMeta& m) {
    found.emplace_back(id, m);
  });

  ASSERT_EQ(found.size(), 2u);
  EXPECT_EQ(found[0].first.page_index, 0);
  EXPECT_EQ(found[1].first.page_index, 1);
}

TEST_F(MetaStoreTest, ScanAll) {
  MetaStore store;
  ASSERT_TRUE(store.Open(metastore_path_));

  store.Put(PageId{MakeBlockId(5, 0), 0}, {TierType::kSSD, 50, 5000});
  store.Put(PageId{MakeBlockId(5, 1), 1}, {TierType::kSSD, 51, 5001});

  size_t count = 0;
  store.ScanAll([&count](PageId, const PageMeta&) { ++count; });
  EXPECT_EQ(count, 2u);
}

TEST_F(MetaStoreTest, RecoveryFullFlow) {
  const size_t kPageSize = 1024;
  const int64_t mtime = 6000;
  PageId id{MakeBlockId(6, 0), 0};
  std::string data(kPageSize, 'x');

  {
    TierManager tier_mgr;
    tier_mgr.AddTier(std::make_unique<MemoryTier>(0));
    tier_mgr.AddTier(std::make_unique<SsdTier>(ssd_path_, kPageSize * 10));

    MetaStore meta;
    ASSERT_TRUE(meta.Open(metastore_path_));

    PageStore store(&tier_mgr, kPageSize, &meta);
    auto s = store.PutPage(id, data, mtime);
    ASSERT_TRUE(s.ok()) << s.message();
  }

  {
    TierManager tier_mgr;
    tier_mgr.AddTier(std::make_unique<MemoryTier>(0));
    tier_mgr.AddTier(std::make_unique<SsdTier>(ssd_path_, kPageSize * 10));

    MetaStore meta;
    ASSERT_TRUE(meta.Open(metastore_path_));

    PageStore store(&tier_mgr, kPageSize, &meta);
    store.RecoverFromMetaStore();

    std::string out;
    auto s = store.GetPage(id, mtime, &out);
    ASSERT_TRUE(s.ok()) << s.message();
    EXPECT_EQ(out, data);
  }
}

TEST_F(MetaStoreTest, RecoveryOrphanCleanup) {
  MetaStore meta;
  ASSERT_TRUE(meta.Open(metastore_path_));

  PageId id{MakeBlockId(7, 0), 0};
  meta.Put(id, {TierType::kSSD, 999, 7000});

  TierManager tier_mgr;
  tier_mgr.AddTier(std::make_unique<MemoryTier>(1024));
  tier_mgr.AddTier(std::make_unique<SsdTier>(ssd_path_, 1024 * 10));

  PageStore store(&tier_mgr, 1024, &meta);
  store.RecoverFromMetaStore();

  EXPECT_FALSE(store.Contains(id));
  EXPECT_FALSE(meta.Get(id).has_value());
}

}  // namespace fluxcache
