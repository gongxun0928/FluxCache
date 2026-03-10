#include "worker/storage/memory_tier.h"
#include "common/status.h"
#include <gtest/gtest.h>

namespace fluxcache {

TEST(MemoryTierTest, AllocateWriteRead) {
  MemoryTier tier(4096);
  TierBlockHandle handle;

  auto s = tier.Allocate(1024, &handle);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_TRUE(handle.valid());
  EXPECT_EQ(tier.UsedCapacity(), 1024u);

  s = tier.Write(handle, 0, "hello");
  ASSERT_TRUE(s.ok()) << s.message();

  std::string out;
  s = tier.Read(handle, 0, 5, &out);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_EQ(out, "hello");

  s = tier.Release(handle);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_EQ(tier.UsedCapacity(), 0u);
}

TEST(MemoryTierTest, CapacityExhausted) {
  MemoryTier tier(100);
  TierBlockHandle h1, h2, h3;

  auto s1 = tier.Allocate(60, &h1);
  ASSERT_TRUE(s1.ok()) << s1.message();

  auto s2 = tier.Allocate(40, &h2);
  ASSERT_TRUE(s2.ok()) << s2.message();
  EXPECT_EQ(tier.UsedCapacity(), 100u);

  auto s3 = tier.Allocate(1, &h3);
  ASSERT_FALSE(s3.ok());
  EXPECT_EQ(s3.code(), StatusCode::kResourceExhausted);

  tier.Release(h1);
  tier.Release(h2);
}

TEST(MemoryTierTest, ReleaseReclaimsCapacity) {
  MemoryTier tier(200);
  TierBlockHandle handle;

  auto s = tier.Allocate(100, &handle);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_EQ(tier.UsedCapacity(), 100u);

  s = tier.Release(handle);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_EQ(tier.UsedCapacity(), 0u);

  s = tier.Allocate(100, &handle);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_EQ(tier.UsedCapacity(), 100u);
}

TEST(MemoryTierTest, InvalidHandle) {
  MemoryTier tier(4096);
  TierBlockHandle invalid;
  invalid.id = 0;

  std::string out;
  EXPECT_FALSE(tier.Write(invalid, 0, "x").ok());
  EXPECT_FALSE(tier.Read(invalid, 0, 1, &out).ok());
  EXPECT_FALSE(tier.Release(invalid).ok());
}

TEST(MemoryTierTest, WriteReadOffset) {
  MemoryTier tier(4096);
  TierBlockHandle handle;

  auto s = tier.Allocate(16, &handle);
  ASSERT_TRUE(s.ok()) << s.message();

  s = tier.Write(handle, 4, "test");
  ASSERT_TRUE(s.ok()) << s.message();

  std::string out;
  s = tier.Read(handle, 4, 4, &out);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_EQ(out, "test");
}

TEST(MemoryTierTest, AllocateZeroSize) {
  MemoryTier tier(4096);
  TierBlockHandle handle;

  auto s = tier.Allocate(0, &handle);
  ASSERT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kInvalidArgument);
}

}  // namespace fluxcache
