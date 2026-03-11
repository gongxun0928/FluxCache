// P2-11: Client local Page memory cache unit tests.

#include "client/cache/client_page_cache.h"
#include "common/types.h"

#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

namespace fluxcache {

class ClientPageCacheTest : public ::testing::Test {
 protected:
  static constexpr size_t kPageSize = 1024;
  static constexpr size_t kCacheCapacity = 4 * kPageSize;  // 4 pages

  void SetUp() override {
    cache_ = std::make_unique<ClientPageCache>(kCacheCapacity);
  }

  std::vector<uint8_t> MakePage(char fill = 'x') {
    return std::vector<uint8_t>(kPageSize, static_cast<uint8_t>(fill));
  }

  std::unique_ptr<ClientPageCache> cache_;
};

TEST_F(ClientPageCacheTest, GetHitMtimeMatch) {
  PageId pid = MakePageId(1, 0);
  auto data = MakePage('a');
  cache_->Put(pid, data, 1000);

  auto got = cache_->Get(pid, 1000);
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->size(), kPageSize);
  EXPECT_EQ(static_cast<char>((*got)[0]), 'a');
}

TEST_F(ClientPageCacheTest, GetMissWhenEmpty) {
  PageId pid = MakePageId(1, 0);
  auto got = cache_->Get(pid, 1000);
  EXPECT_EQ(got, nullptr);
}

TEST_F(ClientPageCacheTest, GetHitMtimeMismatchReturnsMissAndEvicts) {
  PageId pid = MakePageId(1, 0);
  auto data = MakePage('a');
  cache_->Put(pid, data, 1000);

  auto got = cache_->Get(pid, 2000);  // different mtime
  EXPECT_EQ(got, nullptr);
  EXPECT_EQ(cache_->Size(), 0u);  // stale page evicted
}

TEST_F(ClientPageCacheTest, PutEvictsLru) {
  cache_->Put(MakePageId(1, 0), MakePage('1'), 1000);
  cache_->Put(MakePageId(2, 0), MakePage('2'), 1000);
  cache_->Put(MakePageId(3, 0), MakePage('3'), 1000);
  cache_->Put(MakePageId(4, 0), MakePage('4'), 1000);
  EXPECT_EQ(cache_->Size(), 4 * kPageSize);

  cache_->Put(MakePageId(5, 0), MakePage('5'), 1000);
  EXPECT_EQ(cache_->Size(), 4 * kPageSize);

  EXPECT_EQ(cache_->Get(MakePageId(1, 0), 1000), nullptr);  // LRU evicted
  EXPECT_NE(cache_->Get(MakePageId(5, 0), 1000), nullptr);
}

TEST_F(ClientPageCacheTest, Invalidate) {
  PageId pid = MakePageId(1, 0);
  cache_->Put(pid, MakePage('a'), 1000);
  EXPECT_NE(cache_->Get(pid, 1000), nullptr);

  cache_->Invalidate(pid);
  EXPECT_EQ(cache_->Get(pid, 1000), nullptr);
}

TEST_F(ClientPageCacheTest, InvalidateFile) {
  InodeId inode = 42;
  cache_->Put(MakePageId(MakeBlockId(inode, 0), 0), MakePage('a'), 1000);
  cache_->Put(MakePageId(MakeBlockId(inode, 1), 0), MakePage('b'), 1000);
  cache_->Put(MakePageId(MakeBlockId(99, 0), 0), MakePage('c'), 1000);

  cache_->InvalidateFile(inode);

  EXPECT_EQ(cache_->Get(MakePageId(MakeBlockId(inode, 0), 0), 1000), nullptr);
  EXPECT_EQ(cache_->Get(MakePageId(MakeBlockId(inode, 1), 0), 1000), nullptr);
  EXPECT_NE(cache_->Get(MakePageId(MakeBlockId(99, 0), 0), 1000), nullptr);
}

TEST_F(ClientPageCacheTest, Clear) {
  cache_->Put(MakePageId(1, 0), MakePage('a'), 1000);
  cache_->Put(MakePageId(2, 0), MakePage('b'), 1000);

  cache_->Clear();
  EXPECT_EQ(cache_->Size(), 0u);
  EXPECT_EQ(cache_->Get(MakePageId(1, 0), 1000), nullptr);
  EXPECT_EQ(cache_->Get(MakePageId(2, 0), 1000), nullptr);
}

TEST_F(ClientPageCacheTest, SizeAndCapacity) {
  EXPECT_EQ(cache_->Capacity(), kCacheCapacity);
  EXPECT_EQ(cache_->Size(), 0u);

  cache_->Put(MakePageId(1, 0), MakePage(), 1000);
  EXPECT_EQ(cache_->Size(), kPageSize);
}

TEST_F(ClientPageCacheTest, GetUpdatesLruOrder) {
  cache_->Put(MakePageId(1, 0), MakePage('1'), 1000);
  cache_->Put(MakePageId(2, 0), MakePage('2'), 1000);
  cache_->Put(MakePageId(3, 0), MakePage('3'), 1000);

  cache_->Get(MakePageId(1, 0), 1000);  // access page 1, now MRU

  cache_->Put(MakePageId(4, 0), MakePage('4'), 1000);
  cache_->Put(MakePageId(5, 0), MakePage('5'), 1000);

  // Capacity 4 pages. After Put 1,2,3: 3 pages. Get(1) makes 1 MRU.
  // Put 4: 4 pages, no eviction. Put 5: must evict 1. LRU is 3 (or 2).
  // Order after Get(1): [1,2,3] (1 at front). Put 4: [4,1,2,3]. Put 5: evict 3.
  // Remaining: 1, 2, 4, 5. So 3 evicted.
  EXPECT_NE(cache_->Get(MakePageId(1, 0), 1000), nullptr);
  EXPECT_NE(cache_->Get(MakePageId(4, 0), 1000), nullptr);
  EXPECT_NE(cache_->Get(MakePageId(5, 0), 1000), nullptr);
  EXPECT_EQ(cache_->Size(), 4 * kPageSize);
}

}  // namespace fluxcache
