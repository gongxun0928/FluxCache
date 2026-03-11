#include "worker/storage/ssd_tier.h"
#include "common/status.h"
#include <filesystem>
#include <gtest/gtest.h>

namespace fluxcache {

namespace fs = std::filesystem;

class SsdTierTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tmp_dir_ = fs::temp_directory_path() / "fluxcache_ssd_tier_test";
    fs::create_directories(tmp_dir_);
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(tmp_dir_, ec);
  }

  fs::path tmp_dir_;
};

TEST_F(SsdTierTest, AllocateWriteRead) {
  SsdTier tier((tmp_dir_ / "ssd").string(), 4096);
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

TEST_F(SsdTierTest, CapacityExhausted) {
  SsdTier tier((tmp_dir_ / "ssd").string(), 100);
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

TEST_F(SsdTierTest, PersistenceAcrossRestart) {
  std::string root = (tmp_dir_ / "ssd_persist").string();
  uint64_t block_id = 0;
  {
    SsdTier tier(root, 4096);
    TierBlockHandle handle;
    auto s = tier.Allocate(64, &handle);
    ASSERT_TRUE(s.ok()) << s.message();
    block_id = handle.id;
    s = tier.Write(handle, 0, "persisted");
    ASSERT_TRUE(s.ok()) << s.message();
    // Do not Release - block stays on disk for recovery
  }
  {
    SsdTier tier(root, 4096);
    TierBlockHandle handle;
    handle.id = block_id;
    std::string out;
    auto s = tier.Read(handle, 0, 9, &out);
    ASSERT_TRUE(s.ok()) << s.message();
    EXPECT_EQ(out, "persisted");
  }
}

TEST_F(SsdTierTest, InvalidHandle) {
  SsdTier tier((tmp_dir_ / "ssd").string(), 4096);
  TierBlockHandle invalid;
  invalid.id = 0;

  std::string out;
  EXPECT_FALSE(tier.Write(invalid, 0, "x").ok());
  EXPECT_FALSE(tier.Read(invalid, 0, 1, &out).ok());
  EXPECT_FALSE(tier.Release(invalid).ok());
}

TEST_F(SsdTierTest, AllocateZeroSize) {
  SsdTier tier((tmp_dir_ / "ssd").string(), 4096);
  TierBlockHandle handle;

  auto s = tier.Allocate(0, &handle);
  ASSERT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kInvalidArgument);
}

}  // namespace fluxcache
