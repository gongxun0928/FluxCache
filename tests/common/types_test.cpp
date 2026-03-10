#include "common/types.h"
#include <gtest/gtest.h>
#include <unordered_map>

namespace fluxcache {

// -----------------------------------------------------------------------------
// BlockId encode/decode round-trip
// -----------------------------------------------------------------------------

TEST(TypesTest, BlockIdRoundTrip) {
  InodeId inode = 42;
  uint32_t block_index = 3;
  BlockId bid = MakeBlockId(inode, block_index);
  EXPECT_EQ(GetInodeId(bid), inode);
  EXPECT_EQ(GetBlockIndex(bid), block_index);
}

TEST(TypesTest, BlockIdRoundTripZero) {
  BlockId bid = MakeBlockId(0, 0);
  EXPECT_EQ(GetInodeId(bid), 0u);
  EXPECT_EQ(GetBlockIndex(bid), 0u);
}

TEST(TypesTest, BlockIdRoundTripMaxInodeId) {
  InodeId max_inode = kMaxInodeId;
  uint32_t block_index = 0;
  BlockId bid = MakeBlockId(max_inode, block_index);
  EXPECT_EQ(GetInodeId(bid), max_inode);
  EXPECT_EQ(GetBlockIndex(bid), block_index);
}

TEST(TypesTest, BlockIdRoundTripMaxBlockIndex) {
  InodeId inode = 1;
  uint32_t max_block = kMaxBlockIndex;
  BlockId bid = MakeBlockId(inode, max_block);
  EXPECT_EQ(GetInodeId(bid), inode);
  EXPECT_EQ(GetBlockIndex(bid), max_block);
}

TEST(TypesTest, BlockIdRoundTripBothMax) {
  BlockId bid = MakeBlockId(kMaxInodeId, kMaxBlockIndex);
  EXPECT_EQ(GetInodeId(bid), kMaxInodeId);
  EXPECT_EQ(GetBlockIndex(bid), kMaxBlockIndex);
}

// -----------------------------------------------------------------------------
// GetBlockCount / GetBlockLength
// -----------------------------------------------------------------------------

TEST(TypesTest, GetBlockCount) {
  size_t block_size = 64 * 1024 * 1024;  // 64MB
  EXPECT_EQ(GetBlockCount(0, block_size), 0u);
  EXPECT_EQ(GetBlockCount(1, block_size), 1u);
  EXPECT_EQ(GetBlockCount(block_size, block_size), 1u);
  EXPECT_EQ(GetBlockCount(block_size + 1, block_size), 2u);
  EXPECT_EQ(GetBlockCount(200ULL * 1024 * 1024, block_size), 4u);  // 200MB
}

TEST(TypesTest, GetBlockLength) {
  uint64_t file_size = 200ULL * 1024 * 1024;  // 200MB
  size_t block_size = 64 * 1024 * 1024;       // 64MB
  EXPECT_EQ(GetBlockLength(file_size, 0, block_size), block_size);
  EXPECT_EQ(GetBlockLength(file_size, 1, block_size), block_size);
  EXPECT_EQ(GetBlockLength(file_size, 2, block_size), block_size);
  EXPECT_EQ(GetBlockLength(file_size, 3, block_size),
            8ULL * 1024 * 1024);  // 200 - 3*64 = 8MB
  EXPECT_EQ(GetBlockLength(file_size, 4, block_size), 0u);  // out of range
}

TEST(TypesTest, GetBlockCountZeroBlockSize) {
  EXPECT_EQ(GetBlockCount(100, 0), 0u);
}

// -----------------------------------------------------------------------------
// PageId for unordered_map
// -----------------------------------------------------------------------------

TEST(TypesTest, PageIdUnorderedMap) {
  std::unordered_map<PageId, int> m;
  PageId p1 = MakePageId(100, 5);
  PageId p2 = MakePageId(100, 6);
  PageId p3 = MakePageId(101, 5);
  m[p1] = 1;
  m[p2] = 2;
  m[p3] = 3;
  EXPECT_EQ(m.size(), 3u);
  EXPECT_EQ(m[p1], 1);
  EXPECT_EQ(m[p2], 2);
  EXPECT_EQ(m[p3], 3);
  EXPECT_EQ(m[MakePageId(100, 5)], 1);  // same as p1
}

TEST(TypesTest, PageIdEquality) {
  PageId a = MakePageId(42, 7);
  PageId b = MakePageId(42, 7);
  PageId c = MakePageId(42, 8);
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
}

// -----------------------------------------------------------------------------
// MakePageId / GetPagesPerBlock
// -----------------------------------------------------------------------------

TEST(TypesTest, GetPagesPerBlock) {
  size_t block_size = 64 * 1024 * 1024;  // 64MB
  size_t page_size = 1024 * 1024;         // 1MB
  EXPECT_EQ(GetPagesPerBlock(block_size, page_size), 64u);
  EXPECT_EQ(GetPagesPerBlock(0, page_size), 0u);
  EXPECT_EQ(GetPagesPerBlock(block_size, 0), 0u);
}

// -----------------------------------------------------------------------------
// WorkerState
// -----------------------------------------------------------------------------

TEST(TypesTest, WorkerStateValues) {
  EXPECT_EQ(static_cast<uint8_t>(WorkerState::kAlive), 0u);
  EXPECT_EQ(static_cast<uint8_t>(WorkerState::kSuspect), 1u);
  EXPECT_EQ(static_cast<uint8_t>(WorkerState::kDead), 2u);
}

// -----------------------------------------------------------------------------
// TierType
// -----------------------------------------------------------------------------

TEST(TypesTest, TierTypeValues) {
  EXPECT_EQ(static_cast<uint8_t>(TierType::kMemory), 0u);
  EXPECT_EQ(static_cast<uint8_t>(TierType::kSSD), 1u);
  EXPECT_EQ(static_cast<uint8_t>(TierType::kHDD), 2u);
}

}  // namespace fluxcache
