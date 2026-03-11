#include "worker/page/page_store.h"
#include "worker/storage/memory_tier.h"
#include "common/status.h"
#include "common/types.h"
#include <gtest/gtest.h>

namespace fluxcache {

class PageStoreTest : public ::testing::Test {
 protected:
  static constexpr size_t kPageSize = 1024;
  static constexpr size_t kTierCapacity = 64 * 1024;  // 64KB for multiple pages

  void SetUp() override {
    tier_ = std::make_unique<MemoryTier>(kTierCapacity);
    store_ = std::make_unique<PageStore>(tier_.get(), kPageSize);
  }

  std::unique_ptr<MemoryTier> tier_;
  std::unique_ptr<PageStore> store_;
};

TEST_F(PageStoreTest, PutPageThenGetPage) {
  const int64_t mtime = 1000;
  PageId id{MakeBlockId(1, 0), 0};
  std::string data(kPageSize, 'x');

  auto s = store_->PutPage(id, data, mtime);
  ASSERT_TRUE(s.ok()) << s.message();

  std::string out;
  s = store_->GetPage(id, mtime, &out);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_EQ(out, data);
}

TEST_F(PageStoreTest, DeletePageReturnsMiss) {
  const int64_t mtime = 2000;
  PageId id{MakeBlockId(2, 0), 0};
  std::string data(kPageSize, 'a');

  auto s = store_->PutPage(id, data, mtime);
  ASSERT_TRUE(s.ok()) << s.message();

  s = store_->DeletePage(id);
  ASSERT_TRUE(s.ok()) << s.message();

  std::string out;
  s = store_->GetPage(id, mtime, &out);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kNotFound);
}

TEST_F(PageStoreTest, DeleteBlockPagesCleansAll) {
  const int64_t mtime = 3000;
  BlockId block_id = MakeBlockId(3, 0);
  std::string data(kPageSize, 'b');

  store_->PutPage(PageId{block_id, 0}, data, mtime);
  store_->PutPage(PageId{block_id, 1}, data, mtime);
  store_->PutPage(PageId{block_id, 2}, data, mtime);

  EXPECT_TRUE(store_->Contains(PageId{block_id, 0}));
  EXPECT_TRUE(store_->Contains(PageId{block_id, 1}));
  EXPECT_TRUE(store_->Contains(PageId{block_id, 2}));

  auto s = store_->DeleteBlockPages(block_id);
  ASSERT_TRUE(s.ok()) << s.message();

  std::string out;
  EXPECT_FALSE(store_->Contains(PageId{block_id, 0}));
  EXPECT_FALSE(store_->Contains(PageId{block_id, 1}));
  EXPECT_FALSE(store_->Contains(PageId{block_id, 2}));
  EXPECT_FALSE(store_->GetPage(PageId{block_id, 0}, mtime, &out).ok());
  EXPECT_FALSE(store_->GetPage(PageId{block_id, 1}, mtime, &out).ok());
  EXPECT_FALSE(store_->GetPage(PageId{block_id, 2}, mtime, &out).ok());
}

TEST_F(PageStoreTest, MtimeMismatchReturnsMissAndCleans) {
  const int64_t mtime_old = 4000;
  const int64_t mtime_new = 5000;
  PageId id{MakeBlockId(4, 0), 0};
  std::string data(kPageSize, 'c');

  auto s = store_->PutPage(id, data, mtime_old);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_TRUE(store_->Contains(id));

  std::string out;
  s = store_->GetPage(id, mtime_new, &out);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kNotFound);
  EXPECT_FALSE(store_->Contains(id));
}

TEST_F(PageStoreTest, CapacityExhaustedReturnsError) {
  MemoryTier small_tier(kPageSize * 2);
  PageStore small_store(&small_tier, kPageSize);

  PageId id1{MakeBlockId(5, 0), 0};
  PageId id2{MakeBlockId(5, 1), 1};
  std::string data(kPageSize, 'd');

  auto s1 = small_store.PutPage(id1, data, 6000);
  ASSERT_TRUE(s1.ok()) << s1.message();

  auto s2 = small_store.PutPage(id2, data, 6000);
  ASSERT_TRUE(s2.ok()) << s2.message();

  PageId id3{MakeBlockId(5, 2), 2};
  auto s3 = small_store.PutPage(id3, data, 6000);
  EXPECT_FALSE(s3.ok());
  EXPECT_EQ(s3.code(), StatusCode::kResourceExhausted);
}

TEST_F(PageStoreTest, Contains) {
  PageId id{MakeBlockId(6, 0), 0};
  EXPECT_FALSE(store_->Contains(id));

  store_->PutPage(id, std::string(kPageSize, 'e'), 7000);
  EXPECT_TRUE(store_->Contains(id));

  store_->DeletePage(id);
  EXPECT_FALSE(store_->Contains(id));
}

}  // namespace fluxcache
