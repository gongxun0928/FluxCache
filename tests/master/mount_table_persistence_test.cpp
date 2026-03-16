// P4-01: MountTable RocksDB persistence test.
// Validates mount entries survive InodeStore close/reopen cycle.

#include "master/inode_store.h"
#include "master/mount_table.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <string>

namespace fluxcache {

class MountTablePersistenceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_path_ = (std::filesystem::temp_directory_path() /
                "fluxcache_mt_persist_test" /
                std::to_string(std::chrono::steady_clock::now()
                                   .time_since_epoch()
                                   .count()))
                   .string();
    std::filesystem::create_directories(db_path_);
  }

  void TearDown() override {
    try {
      std::filesystem::remove_all(
          std::filesystem::temp_directory_path() /
          "fluxcache_mt_persist_test");
    } catch (...) {}
  }

  std::string db_path_;
};

TEST_F(MountTablePersistenceTest, MountSurvivesReopenCycle) {
  {
    InodeStore store;
    ASSERT_TRUE(store.Open(db_path_));

    MountTable mt;
    mt.BindStore(&store);

    ASSERT_TRUE(mt.Mount("/data", "local:///tmp/data").ok());
    ASSERT_TRUE(mt.Mount("/logs", "s3://mybucket/logs").ok());

    auto mounts = mt.ListMounts();
    EXPECT_EQ(mounts.size(), 2u);

    store.Close();
  }

  {
    InodeStore store;
    ASSERT_TRUE(store.Open(db_path_));

    MountTable mt;
    mt.BindStore(&store);
    mt.RecoverFromStore();

    auto mounts = mt.ListMounts();
    ASSERT_EQ(mounts.size(), 2u);
    EXPECT_EQ(mounts[0], "/data");
    EXPECT_EQ(mounts[1], "/logs");

    std::string ufs_uri, ufs_path;
    ASSERT_TRUE(mt.Resolve("/data/file.txt", &ufs_uri, &ufs_path).ok());
    EXPECT_EQ(ufs_uri, "local:///tmp/data");
    EXPECT_EQ(ufs_path, "file.txt");

    ASSERT_TRUE(mt.Resolve("/logs/app.log", &ufs_uri, &ufs_path).ok());
    EXPECT_EQ(ufs_uri, "s3://mybucket/logs");
    EXPECT_EQ(ufs_path, "app.log");

    store.Close();
  }
}

TEST_F(MountTablePersistenceTest, UnmountDeletesFromStore) {
  {
    InodeStore store;
    ASSERT_TRUE(store.Open(db_path_));

    MountTable mt;
    mt.BindStore(&store);

    ASSERT_TRUE(mt.Mount("/data", "local:///tmp/data").ok());
    ASSERT_TRUE(mt.Mount("/logs", "s3://mybucket/logs").ok());
    ASSERT_TRUE(mt.Unmount("/logs").ok());

    store.Close();
  }

  {
    InodeStore store;
    ASSERT_TRUE(store.Open(db_path_));

    MountTable mt;
    mt.BindStore(&store);
    mt.RecoverFromStore();

    auto mounts = mt.ListMounts();
    ASSERT_EQ(mounts.size(), 1u);
    EXPECT_EQ(mounts[0], "/data");

    store.Close();
  }
}

TEST_F(MountTablePersistenceTest, WithoutStoreWorksAsMemoryOnly) {
  MountTable mt;
  ASSERT_TRUE(mt.Mount("/data", "local:///tmp/data").ok());

  auto mounts = mt.ListMounts();
  ASSERT_EQ(mounts.size(), 1u);

  ASSERT_TRUE(mt.Unmount("/data").ok());
  mounts = mt.ListMounts();
  EXPECT_TRUE(mounts.empty());
}

TEST_F(MountTablePersistenceTest, MasterRestartPreservesMount) {
  namespace fs = std::filesystem;
  auto master_db = db_path_ + "/master_db";
  fs::create_directories(master_db);

  {
    InodeStore store;
    ASSERT_TRUE(store.Open(master_db));

    MountTable mt;
    mt.BindStore(&store);
    ASSERT_TRUE(mt.Mount("/mnt", "local:///ufs/root").ok());
    ASSERT_TRUE(mt.Mount("/archive", "s3://archive-bucket").ok());

    store.Close();
  }

  {
    InodeStore store;
    ASSERT_TRUE(store.Open(master_db));

    MountTable mt;
    mt.BindStore(&store);
    mt.RecoverFromStore();

    auto mounts = mt.ListMounts();
    ASSERT_EQ(mounts.size(), 2u);

    std::string uri, path;
    ASSERT_TRUE(mt.Resolve("/mnt/subdir/file.dat", &uri, &path).ok());
    EXPECT_EQ(uri, "local:///ufs/root");
    EXPECT_EQ(path, "subdir/file.dat");

    ASSERT_TRUE(mt.Resolve("/archive/2026/data.csv", &uri, &path).ok());
    EXPECT_EQ(uri, "s3://archive-bucket");
    EXPECT_EQ(path, "2026/data.csv");

    store.Close();
  }
}

}  // namespace fluxcache
