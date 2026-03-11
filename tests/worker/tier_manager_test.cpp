#include "worker/storage/hdd_tier.h"
#include "worker/storage/memory_tier.h"
#include "worker/storage/ssd_tier.h"
#include "worker/storage/tier_manager.h"
#include "common/status.h"
#include <filesystem>
#include <gtest/gtest.h>

namespace fluxcache {

namespace fs = std::filesystem;

class TierManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tmp_dir_ = fs::temp_directory_path() / "fluxcache_tier_manager_test";
    fs::create_directories(tmp_dir_);
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(tmp_dir_, ec);
  }

  fs::path tmp_dir_;
};

TEST_F(TierManagerTest, PrioritySelection) {
  TierManager mgr;
  mgr.AddTier(std::make_unique<MemoryTier>(50));
  mgr.AddTier(std::make_unique<SsdTier>((tmp_dir_ / "ssd").string(), 100));
  mgr.AddTier(std::make_unique<HddTier>((tmp_dir_ / "hdd").string(), 200));

  TierBlockHandle h1, h2, h3, h4;
  auto s1 = mgr.Allocate(30, &h1);
  ASSERT_TRUE(s1.ok()) << s1.message();
  EXPECT_EQ(mgr.UsedCapacity(), 30u);

  auto s2 = mgr.Allocate(30, &h2);
  ASSERT_TRUE(s2.ok()) << s2.message();
  EXPECT_EQ(mgr.UsedCapacity(), 60u);

  auto s3 = mgr.Allocate(50, &h3);
  ASSERT_TRUE(s3.ok()) << s3.message();
  EXPECT_EQ(mgr.UsedCapacity(), 110u);

  auto s4 = mgr.Allocate(100, &h4);
  ASSERT_TRUE(s4.ok()) << s4.message();
  EXPECT_EQ(mgr.UsedCapacity(), 210u);
  // Total capacity 350, used 210, remaining 140. Allocate 141 to exhaust.
  TierBlockHandle h5;
  auto s5 = mgr.Allocate(141, &h5);
  ASSERT_FALSE(s5.ok());
  EXPECT_EQ(s5.code(), StatusCode::kResourceExhausted);

  mgr.Release(h1);
  mgr.Release(h2);
  mgr.Release(h3);
  mgr.Release(h4);
  // h5 was not allocated
}

TEST_F(TierManagerTest, AllocateWriteRead) {
  TierManager mgr;
  mgr.AddTier(std::make_unique<MemoryTier>(1024));
  mgr.AddTier(std::make_unique<SsdTier>((tmp_dir_ / "ssd_alloc").string(), 1024));

  TierBlockHandle handle;
  auto s = mgr.Allocate(64, &handle);
  ASSERT_TRUE(s.ok()) << s.message();

  const std::string data = "tier_manager_data";
  s = mgr.Write(handle, 0, data);
  ASSERT_TRUE(s.ok()) << s.message();

  std::string out;
  s = mgr.Read(handle, 0, data.size(), &out);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_EQ(out, data);

  s = mgr.Release(handle);
  ASSERT_TRUE(s.ok()) << s.message();
}

TEST_F(TierManagerTest, CapacityStats) {
  TierManager mgr;
  mgr.AddTier(std::make_unique<MemoryTier>(100));
  mgr.AddTier(std::make_unique<SsdTier>((tmp_dir_ / "ssd").string(), 200));
  mgr.AddTier(std::make_unique<HddTier>((tmp_dir_ / "hdd").string(), 300));

  EXPECT_EQ(mgr.CapacityLimit(), 600u);
  EXPECT_EQ(mgr.UsedCapacity(), 0u);

  TierBlockHandle h1, h2;
  mgr.Allocate(50, &h1);
  mgr.Allocate(100, &h2);
  EXPECT_EQ(mgr.UsedCapacity(), 150u);

  mgr.Release(h1);
  mgr.Release(h2);
  EXPECT_EQ(mgr.UsedCapacity(), 0u);
}

TEST_F(TierManagerTest, InvalidHandle) {
  TierManager mgr;
  mgr.AddTier(std::make_unique<MemoryTier>(1024));

  TierBlockHandle invalid;
  invalid.id = 0;

  std::string out;
  EXPECT_FALSE(mgr.Write(invalid, 0, "x").ok());
  EXPECT_FALSE(mgr.Read(invalid, 0, 1, &out).ok());
  EXPECT_FALSE(mgr.Release(invalid).ok());
}

TEST_F(TierManagerTest, UnknownHandle) {
  TierManager mgr;
  mgr.AddTier(std::make_unique<MemoryTier>(1024));

  TierBlockHandle handle;
  mgr.Allocate(64, &handle);
  mgr.Release(handle);

  std::string out;
  EXPECT_FALSE(mgr.Write(handle, 0, "x").ok());
  EXPECT_FALSE(mgr.Read(handle, 0, 1, &out).ok());
  EXPECT_FALSE(mgr.Release(handle).ok());
}

}  // namespace fluxcache
