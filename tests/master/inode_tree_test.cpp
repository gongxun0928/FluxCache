#include "master/inode_tree.h"
#include <filesystem>
#include <gtest/gtest.h>
#include <cstdlib>
#include <vector>
#include <set>
#include <string>

namespace fluxcache {

namespace {

std::string TempDbPath() {
  std::filesystem::path tmp = std::filesystem::temp_directory_path();
  tmp /= "fluxcache_inode_test_XXXXXX";
  std::string s = tmp.string();
  // mkdtemp requires mutable char*
  std::vector<char> buf(s.begin(), s.end());
  buf.push_back('\0');
  char* result = mkdtemp(buf.data());
  if (!result) return "";
  return std::string(result);
}

}  // namespace

TEST(InodeTreeTest, InitRoot) {
  std::string db_path = TempDbPath();
  ASSERT_FALSE(db_path.empty()) << "mkdtemp failed";

  InodeTree tree(db_path);
  ASSERT_TRUE(tree.InitOrRecover());
  EXPECT_TRUE(tree.is_ready());

  auto root_id = tree.LookupPath("/");
  ASSERT_TRUE(root_id.has_value());
  EXPECT_EQ(*root_id, 1u);

  auto entry = tree.GetInode(1);
  ASSERT_TRUE(entry.has_value());
  EXPECT_TRUE(entry->is_directory());
  EXPECT_EQ(entry->name, "/");

  std::filesystem::remove_all(db_path);
}

TEST(InodeTreeTest, Recover) {
  std::string db_path = TempDbPath();
  ASSERT_FALSE(db_path.empty());

  {
    InodeTree tree(db_path);
    ASSERT_TRUE(tree.InitOrRecover());
    auto id = tree.CreateFile("/f1");
    ASSERT_TRUE(id.has_value());
    EXPECT_GE(*id, 2u);
  }

  {
    InodeTree tree2(db_path);
    ASSERT_TRUE(tree2.InitOrRecover());
    auto root = tree2.LookupPath("/");
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(*root, 1u);

    auto f1 = tree2.LookupPath("/f1");
    ASSERT_TRUE(f1.has_value());
    EXPECT_GE(*f1, 2u);
  }

  std::filesystem::remove_all(db_path);
}

TEST(InodeTreeTest, LookupPathStableInodeId) {
  std::string db_path = TempDbPath();
  ASSERT_FALSE(db_path.empty());

  InodeTree tree(db_path);
  ASSERT_TRUE(tree.InitOrRecover());

  auto id1 = tree.CreateFile("/foo");
  ASSERT_TRUE(id1.has_value());

  auto lookup1 = tree.LookupPath("/foo");
  ASSERT_TRUE(lookup1.has_value());
  EXPECT_EQ(*lookup1, *id1);

  auto lookup2 = tree.LookupPath("/foo");
  ASSERT_TRUE(lookup2.has_value());
  EXPECT_EQ(*lookup2, *id1);

  std::filesystem::remove_all(db_path);
}

TEST(InodeTreeTest, CreateFileWritesInodesAndEdges) {
  std::string db_path = TempDbPath();
  ASSERT_FALSE(db_path.empty());

  InodeTree tree(db_path);
  ASSERT_TRUE(tree.InitOrRecover());

  auto id = tree.CreateFile("/myfile");
  ASSERT_TRUE(id.has_value());

  auto entry = tree.GetInode(*id);
  ASSERT_TRUE(entry.has_value());
  EXPECT_FALSE(entry->is_directory());
  EXPECT_EQ(entry->name, "myfile");
  EXPECT_EQ(entry->parent_id, 1u);

  auto list = tree.ListDirectory(1);
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].first, "myfile");
  EXPECT_EQ(list[0].second, *id);

  std::filesystem::remove_all(db_path);
}

TEST(InodeTreeTest, CreateDirectoryWritesInodesAndEdges) {
  std::string db_path = TempDbPath();
  ASSERT_FALSE(db_path.empty());

  InodeTree tree(db_path);
  ASSERT_TRUE(tree.InitOrRecover());

  auto id = tree.CreateDirectory("/mydir");
  ASSERT_TRUE(id.has_value());

  auto entry = tree.GetInode(*id);
  ASSERT_TRUE(entry.has_value());
  EXPECT_TRUE(entry->is_directory());
  EXPECT_EQ(entry->name, "mydir");

  auto list = tree.ListDirectory(1);
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].first, "mydir");
  EXPECT_EQ(list[0].second, *id);

  std::filesystem::remove_all(db_path);
}

TEST(InodeTreeTest, DeleteInodeImmediate) {
  std::string db_path = TempDbPath();
  ASSERT_FALSE(db_path.empty());

  InodeTree tree(db_path);
  ASSERT_TRUE(tree.InitOrRecover());

  auto id = tree.CreateFile("/to_delete");
  ASSERT_TRUE(id.has_value());

  EXPECT_TRUE(tree.DeleteInode(*id));
  EXPECT_FALSE(tree.LookupPath("/to_delete").has_value());
  EXPECT_FALSE(tree.GetInode(*id).has_value());

  auto list = tree.ListDirectory(1);
  EXPECT_EQ(list.size(), 0u);

  std::filesystem::remove_all(db_path);
}

TEST(InodeTreeTest, ListDirectory) {
  std::string db_path = TempDbPath();
  ASSERT_FALSE(db_path.empty());

  InodeTree tree(db_path);
  ASSERT_TRUE(tree.InitOrRecover());

  tree.CreateFile("/a");
  tree.CreateDirectory("/b");
  tree.CreateFile("/c");

  auto list = tree.ListDirectory(1);
  ASSERT_EQ(list.size(), 3u);
  // Order may vary
  std::set<std::string> names;
  for (const auto& [n, _] : list) names.insert(n);
  EXPECT_TRUE(names.count("a"));
  EXPECT_TRUE(names.count("b"));
  EXPECT_TRUE(names.count("c"));

  std::filesystem::remove_all(db_path);
}

TEST(InodeTreeTest, RestartRecovery) {
  std::string db_path = TempDbPath();
  ASSERT_FALSE(db_path.empty());

  InodeId file_id;
  InodeId dir_id;
  {
    InodeTree tree(db_path);
    ASSERT_TRUE(tree.InitOrRecover());
    auto fid = tree.CreateFile("/persisted_file");
    auto did = tree.CreateDirectory("/persisted_dir");
    ASSERT_TRUE(fid.has_value());
    ASSERT_TRUE(did.has_value());
    file_id = *fid;
    dir_id = *did;
  }

  {
    InodeTree tree2(db_path);
    ASSERT_TRUE(tree2.InitOrRecover());

    auto f = tree2.LookupPath("/persisted_file");
    auto d = tree2.LookupPath("/persisted_dir");
    ASSERT_TRUE(f.has_value());
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(*f, file_id);
    EXPECT_EQ(*d, dir_id);

    auto list = tree2.ListDirectory(1);
    EXPECT_EQ(list.size(), 2u);
  }

  std::filesystem::remove_all(db_path);
}

TEST(InodeTreeTest, CreateNestedPath) {
  std::string db_path = TempDbPath();
  ASSERT_FALSE(db_path.empty());

  InodeTree tree(db_path);
  ASSERT_TRUE(tree.InitOrRecover());

  auto d = tree.CreateDirectory("/a");
  ASSERT_TRUE(d.has_value());

  auto f = tree.CreateFile("/a/b");
  ASSERT_TRUE(f.has_value());

  auto lookup = tree.LookupPath("/a/b");
  ASSERT_TRUE(lookup.has_value());
  EXPECT_EQ(*lookup, *f);

  auto list = tree.ListDirectory(*d);
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].first, "b");

  std::filesystem::remove_all(db_path);
}

}  // namespace fluxcache
