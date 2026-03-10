#include "master/mount_table.h"
#include "common/status.h"
#include <gtest/gtest.h>

namespace fluxcache {

TEST(MountTableTest, MountAndResolve) {
  MountTable mt;
  auto s = mt.Mount("/data", "local:///tmp/data");
  ASSERT_TRUE(s.ok()) << s.message();

  std::string ufs_uri, ufs_path;
  s = mt.Resolve("/data/file", &ufs_uri, &ufs_path);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_EQ(ufs_uri, "local:///tmp/data");
  EXPECT_EQ(ufs_path, "file");
}

TEST(MountTableTest, ResolveLongestPrefixMatch) {
  MountTable mt;
  ASSERT_TRUE(mt.Mount("/data", "local:///tmp/data").ok());
  ASSERT_TRUE(mt.Mount("/data/hot", "local:///tmp/hot").ok());

  std::string ufs_uri, ufs_path;
  auto s = mt.Resolve("/data/hot/file", &ufs_uri, &ufs_path);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_EQ(ufs_uri, "local:///tmp/hot");
  EXPECT_EQ(ufs_path, "file");
}

TEST(MountTableTest, ResolveDuplicateMountReturnsError) {
  MountTable mt;
  ASSERT_TRUE(mt.Mount("/data", "local:///tmp/data").ok());
  auto s = mt.Mount("/data", "local:///other");
  ASSERT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kInvalidArgument);
  EXPECT_NE(s.message().find("already mounted"), std::string::npos);
}

TEST(MountTableTest, ResolveNoMountPointReturnsNotFound) {
  MountTable mt;
  ASSERT_TRUE(mt.Mount("/data", "local:///tmp/data").ok());

  std::string ufs_uri, ufs_path;
  auto s = mt.Resolve("/unknown/path", &ufs_uri, &ufs_path);
  ASSERT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kNotFound);
  EXPECT_NE(s.message().find("no mount point"), std::string::npos);
}

TEST(MountTableTest, ListMounts) {
  MountTable mt;
  ASSERT_TRUE(mt.Mount("/data", "local:///tmp/data").ok());
  ASSERT_TRUE(mt.Mount("/data/hot", "local:///tmp/hot").ok());

  auto paths = mt.ListMounts();
  ASSERT_EQ(paths.size(), 2u);
  EXPECT_EQ(paths[0], "/data");
  EXPECT_EQ(paths[1], "/data/hot");
}

TEST(MountTableTest, Unmount) {
  MountTable mt;
  ASSERT_TRUE(mt.Mount("/data", "local:///tmp/data").ok());
  ASSERT_TRUE(mt.Mount("/data/hot", "local:///tmp/hot").ok());

  auto s = mt.Unmount("/data/hot");
  ASSERT_TRUE(s.ok()) << s.message();

  std::string ufs_uri, ufs_path;
  s = mt.Resolve("/data/hot/file", &ufs_uri, &ufs_path);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_EQ(ufs_uri, "local:///tmp/data");
  EXPECT_EQ(ufs_path, "hot/file");
}

TEST(MountTableTest, UnmountNonExistentReturnsNotFound) {
  MountTable mt;
  auto s = mt.Unmount("/nonexistent");
  ASSERT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kNotFound);
}

}  // namespace fluxcache
