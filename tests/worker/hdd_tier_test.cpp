#include "worker/storage/hdd_tier.h"
#include "common/status.h"
#include <filesystem>
#include <gtest/gtest.h>

namespace fluxcache {

namespace fs = std::filesystem;

class HddTierTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tmp_dir_ = fs::temp_directory_path() / "fluxcache_hdd_tier_test";
    fs::create_directories(tmp_dir_);
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(tmp_dir_, ec);
  }

  fs::path tmp_dir_;
};

TEST_F(HddTierTest, AllocateWriteRead) {
  HddTier tier((tmp_dir_ / "hdd").string(), 4096);
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

TEST_F(HddTierTest, CapacityExhausted) {
  HddTier tier((tmp_dir_ / "hdd").string(), 100);
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

TEST_F(HddTierTest, PersistenceAcrossRestart) {
  std::string root = (tmp_dir_ / "hdd_persist").string();
  uint64_t block_id = 0;
  {
    HddTier tier(root, 4096);
    TierBlockHandle handle;
    auto s = tier.Allocate(64, &handle);
    ASSERT_TRUE(s.ok()) << s.message();
    block_id = handle.id;
    s = tier.Write(handle, 0, "persisted");
    ASSERT_TRUE(s.ok()) << s.message();
    // Do not Release - block stays on disk for recovery
  }
  {
    HddTier tier(root, 4096);
    TierBlockHandle handle;
    handle.id = block_id;
    std::string out;
    auto s = tier.Read(handle, 0, 9, &out);
    ASSERT_TRUE(s.ok()) << s.message();
    EXPECT_EQ(out, "persisted");
  }
}

TEST_F(HddTierTest, ReleaseReclaimsCapacity) {
  HddTier tier((tmp_dir_ / "hdd").string(), 200);
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

}  // namespace fluxcache
