#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "master.pb.h"
#include "master/inode_tree.h"
#include "master/master_service_impl.h"
#include "master/mount_table.h"
#include "master/path_resolver.h"
#include "ufs/fake_ufs.h"
#include "ufs/local_ufs.h"
#include "ufs/ufs_factory.h"

namespace fluxcache {

namespace {}  // namespace

class SyncFromUfsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    namespace fs = std::filesystem;
    const auto unique_suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    base_path_ = fs::temp_directory_path() /
                 ("fluxcache_sync_ufs_test_" + unique_suffix);
    fs::create_directories(base_path_);
    ufs_root_ = (base_path_ / "ufs").string() + "/";
    db_path_ = (base_path_ / "db").string();
    fs::create_directories(ufs_root_);
    ASSERT_FALSE(ufs_root_.empty());

    tree_ = std::make_unique<InodeTree>(db_path_);
    ASSERT_TRUE(tree_->InitOrRecover()) << "InodeTree init failed";
  }

  void TearDown() override {
    tree_.reset();
    std::filesystem::remove_all(base_path_);
  }

  std::filesystem::path base_path_;
  std::string ufs_root_;
  std::string db_path_;
  std::unique_ptr<InodeTree> tree_;
};

TEST_F(SyncFromUfsTest, SyncFromUfsResolveAndListWork) {
  auto ufs = std::make_unique<LocalUFS>(ufs_root_);
  ASSERT_TRUE(ufs->Mkdirs("dir").ok());
  ASSERT_TRUE(ufs->Write("dir/file.txt", 0, "hello").ok());

  std::string ufs_uri = "local://" + ufs_root_;
  MountTable mt;
  ASSERT_TRUE(mt.Mount("/data", ufs_uri).ok());

  std::string resolved_uri, resolved_path;
  Status s = mt.Resolve("/data/dir/file.txt", &resolved_uri, &resolved_path);
  ASSERT_TRUE(s.ok()) << s.message();
  EXPECT_EQ(resolved_path, "dir/file.txt");

  std::unique_ptr<UFS> ufs2;
  s = CreateUFS("local", ufs_root_, &ufs2);
  ASSERT_TRUE(s.ok()) << "CreateUFS failed: " << s.message();

  std::vector<FileStatus> entries;
  s = ufs2->List("dir", &entries);
  ASSERT_TRUE(s.ok()) << s.message();
  ASSERT_GE(entries.size(), 1u);
  bool found = false;
  for (const auto &e : entries) {
    if (e.path == "file.txt") found = true;
  }
  EXPECT_TRUE(found);
}

TEST_F(SyncFromUfsTest, SyncFromUfsCreatesInodesForUfsFiles) {
  auto ufs = std::make_unique<LocalUFS>(ufs_root_);
  ASSERT_TRUE(ufs->Write("file.txt", 0, "hello").ok());
  ASSERT_TRUE(ufs->Mkdirs("subdir").ok());

  MountTable mt;
  std::string ufs_uri = "local://" + ufs_root_;
  ASSERT_TRUE(mt.Mount("/mnt", ufs_uri).ok());

  PathResolver resolver(tree_.get(), &mt);

  Status s = resolver.SyncFromUfs("/mnt");
  ASSERT_TRUE(s.ok()) << s.message();

  auto mnt_id = tree_->LookupPath("/mnt");
  ASSERT_TRUE(mnt_id.has_value());
  EXPECT_TRUE(tree_->GetInode(*mnt_id)->is_directory());

  auto file_id = tree_->LookupPath("/mnt/file.txt");
  ASSERT_TRUE(file_id.has_value());
  EXPECT_FALSE(tree_->GetInode(*file_id)->is_directory());

  auto subdir_id = tree_->LookupPath("/mnt/subdir");
  ASSERT_TRUE(subdir_id.has_value());
  EXPECT_TRUE(tree_->GetInode(*subdir_id)->is_directory());
}

TEST_F(SyncFromUfsTest, ListDirectoryReturnsSyncedEntries) {
  auto ufs = std::make_unique<LocalUFS>(ufs_root_);
  ASSERT_TRUE(ufs->Write("a.txt", 0, "a").ok());
  ASSERT_TRUE(ufs->Mkdirs("b").ok());
  ASSERT_TRUE(ufs->Write("c.txt", 0, "c").ok());

  MountTable mt;
  ASSERT_TRUE(mt.Mount("/mnt", "local://" + ufs_root_).ok());

  PathResolver resolver(tree_.get(), &mt);
  auto list = resolver.ListDirectory("/mnt");

  ASSERT_EQ(list.size(), 3u);
  std::vector<std::string> names;
  for (const auto &[n, _] : list) names.push_back(n);
  EXPECT_TRUE(std::find(names.begin(), names.end(), "a.txt") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "b") != names.end());
  EXPECT_TRUE(std::find(names.begin(), names.end(), "c.txt") != names.end());
}

TEST_F(SyncFromUfsTest, ResolveOrSyncFillsMissingInode) {
  auto ufs = std::make_unique<LocalUFS>(ufs_root_);
  ASSERT_TRUE(ufs->Write("missing.txt", 0, "content").ok());

  MountTable mt;
  ASSERT_TRUE(mt.Mount("/mnt", "local://" + ufs_root_).ok());

  PathResolver resolver(tree_.get(), &mt);

  EXPECT_FALSE(tree_->LookupPath("/mnt/missing.txt").has_value());

  auto id = resolver.ResolveOrSync("/mnt/missing.txt");
  ASSERT_TRUE(id.has_value());
  EXPECT_TRUE(tree_->LookupPath("/mnt/missing.txt").has_value());
}

TEST_F(SyncFromUfsTest, RenameSyncsDestinationParentBeforeCrossDirectoryMove) {
  auto fake = std::make_unique<FakeUfs>();
  fake->Mkdirs("dst");
  fake->AddFile("src/file.txt", 7, 1234);
  RegisterFakeUfsForTest("rename-dst-parent", std::move(fake));

  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;
  proto::MountRequest mount_req;
  mount_req.set_path("/mnt");
  mount_req.set_ufs_uri("fake://rename-dst-parent");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl.Mount(&ctx, &mount_req, &mount_resp).ok());

  ASSERT_TRUE(tree_->CreateDirectory("/mnt").has_value());
  ASSERT_TRUE(tree_->LookupPath("/mnt").has_value());
  ASSERT_TRUE(tree_->CreateDirectory("/mnt/src").has_value());
  ASSERT_TRUE(tree_->LookupPath("/mnt/src").has_value());
  ASSERT_TRUE(
      tree_->CreateFile("/mnt/src/file.txt", 7, 4096, 1234).has_value());
  ASSERT_TRUE(tree_->LookupPath("/mnt/src/file.txt").has_value());

  EXPECT_TRUE(tree_->LookupPath("/mnt/src/file.txt").has_value());
  EXPECT_FALSE(tree_->LookupPath("/mnt/dst").has_value());

  proto::RenameRequest rename_req;
  rename_req.set_src_path("/mnt/src/file.txt");
  rename_req.set_dst_path("/mnt/dst/file.txt");
  proto::RenameResponse rename_resp;

  auto rename_status = impl.Rename(&ctx, &rename_req, &rename_resp);

  ASSERT_TRUE(rename_status.ok()) << rename_status.error_message();
  EXPECT_FALSE(tree_->LookupPath("/mnt/src/file.txt").has_value());
  auto dst_id = tree_->LookupPath("/mnt/dst/file.txt");
  ASSERT_TRUE(dst_id.has_value());
  auto dst_entry = tree_->GetInode(*dst_id);
  ASSERT_TRUE(dst_entry.has_value());
  EXPECT_FALSE(dst_entry->is_directory());
}

TEST_F(SyncFromUfsTest, UnmountWithActiveInodesReturnsError) {
  auto ufs = std::make_unique<LocalUFS>(ufs_root_);
  ASSERT_TRUE(ufs->Write("file.txt", 0, "x").ok());

  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;
  proto::MountRequest mount_req;
  mount_req.set_path("/mnt");
  mount_req.set_ufs_uri("local://" + ufs_root_);
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl.Mount(&ctx, &mount_req, &mount_resp).ok());

  proto::GetFileInfoRequest fi_req;
  fi_req.set_path("/mnt/file.txt");
  proto::GetFileInfoResponse fi_resp;
  auto fi_status = impl.GetFileInfo(&ctx, &fi_req, &fi_resp);
  ASSERT_TRUE(fi_status.ok()) << fi_status.error_message();
  EXPECT_GT(fi_resp.file_info().inode_id(), 0u);

  proto::UnmountRequest unmount_req;
  unmount_req.set_path("/mnt");
  proto::UnmountResponse unmount_resp;
  auto unmount_status = impl.Unmount(&ctx, &unmount_req, &unmount_resp);

  EXPECT_FALSE(unmount_status.ok())
      << "Unmount should fail when mount has active inodes";
  EXPECT_EQ(unmount_status.error_code(),
            ::grpc::StatusCode::FAILED_PRECONDITION);
  EXPECT_NE(std::string(unmount_status.error_message()).find("active inodes"),
            std::string::npos);
}

TEST_F(SyncFromUfsTest, UnmountWithoutInodesSucceeds) {
  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;
  proto::MountRequest mount_req;
  mount_req.set_path("/mnt");
  mount_req.set_ufs_uri("local://" + ufs_root_);
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl.Mount(&ctx, &mount_req, &mount_resp).ok());

  proto::UnmountRequest unmount_req;
  unmount_req.set_path("/mnt");
  proto::UnmountResponse unmount_resp;
  auto status = impl.Unmount(&ctx, &unmount_req, &unmount_resp);

  ASSERT_TRUE(status.ok()) << status.error_message();

  proto::ListMountsRequest list_req;
  proto::ListMountsResponse list_resp;
  ASSERT_TRUE(impl.ListMounts(&ctx, &list_req, &list_resp).ok());
  EXPECT_EQ(list_resp.paths_size(), 0);
}

}  // namespace fluxcache
