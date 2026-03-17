#include "master/ha/raft_node.h"
#include "master/inode_tree.h"
#include "master/master_service_impl.h"
#include "master.pb.h"
#include "ufs/fake_ufs.h"
#include "ufs/ufs_factory.h"

#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include <filesystem>
#include <thread>

namespace fluxcache {

namespace {

std::string TempDir(const char* prefix) {
  std::filesystem::path tmp = std::filesystem::temp_directory_path();
  tmp /= prefix;
  std::string s = tmp.string();
  std::vector<char> buf(s.begin(), s.end());
  buf.push_back('\0');
  char* result = mkdtemp(buf.data());
  if (!result) return "";
  return std::string(result);
}

}  // namespace

// Integration test: MasterServiceImpl write RPCs go through Raft replication.
class RaftWritePathTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_path_ = TempDir("fluxcache_raft_wp_db_XXXXXX");
    snap_path_ = TempDir("fluxcache_raft_wp_snap_XXXXXX");
    raft_state_path_ = TempDir("fluxcache_raft_wp_state_XXXXXX");
    ASSERT_FALSE(db_path_.empty());
    ASSERT_FALSE(snap_path_.empty());
    ASSERT_FALSE(raft_state_path_.empty());

    tree_ = std::make_unique<InodeTree>(db_path_);
    ASSERT_TRUE(tree_->InitOrRecover());

    impl_ = std::make_unique<MasterServiceImpl>(tree_.get(), nullptr);
    impl_->BindMountTableStore(tree_->store());
    impl_->RecoverMountTable();

    raft_node_ = std::make_unique<RaftNode>(
        1, "localhost:19501", tree_.get(), impl_->mount_table_ptr(),
        snap_path_, raft_state_path_, impl_.get());
    ASSERT_TRUE(raft_node_->Start(19501, {}, 50, 100, 200, 10000, 5000));

    for (int i = 0; i < 50; ++i) {
      if (raft_node_->IsLeader()) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_TRUE(raft_node_->IsLeader());

    impl_->SetRaftNode(raft_node_.get());
  }

  void TearDown() override {
    impl_.reset();
    if (raft_node_) raft_node_->Shutdown();
    raft_node_.reset();
    tree_.reset();
    std::error_code ec;
    std::filesystem::remove_all(db_path_, ec);
    std::filesystem::remove_all(snap_path_, ec);
    std::filesystem::remove_all(raft_state_path_, ec);
  }

  std::string db_path_;
  std::string snap_path_;
  std::string raft_state_path_;
  std::unique_ptr<InodeTree> tree_;
  std::unique_ptr<MasterServiceImpl> impl_;
  std::unique_ptr<RaftNode> raft_node_;
};

TEST_F(RaftWritePathTest, MountThroughRaft) {
  ::grpc::ServerContext ctx;

  proto::MountRequest req;
  req.set_path("/data");
  req.set_ufs_uri("local:///tmp/test");
  proto::MountResponse resp;

  auto status = impl_->Mount(&ctx, &req, &resp);
  ASSERT_TRUE(status.ok()) << status.error_message();

  proto::ListMountsRequest list_req;
  proto::ListMountsResponse list_resp;
  ASSERT_TRUE(impl_->ListMounts(&ctx, &list_req, &list_resp).ok());
  ASSERT_EQ(list_resp.paths_size(), 1);
  EXPECT_EQ(list_resp.paths(0), "/data");
}

TEST_F(RaftWritePathTest, DuplicateMountReturnsInvalidArgument) {
  ::grpc::ServerContext ctx;

  proto::MountRequest req;
  req.set_path("/data");
  req.set_ufs_uri("local:///tmp/test");
  proto::MountResponse resp;
  ASSERT_TRUE(impl_->Mount(&ctx, &req, &resp).ok());

  proto::MountResponse resp2;
  auto status = impl_->Mount(&ctx, &req, &resp2);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_F(RaftWritePathTest, UnmountThroughRaft) {
  ::grpc::ServerContext ctx;

  // Mount first
  proto::MountRequest mount_req;
  mount_req.set_path("/tmp_mnt");
  mount_req.set_ufs_uri("local:///tmp/test");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl_->Mount(&ctx, &mount_req, &mount_resp).ok());

  // Unmount via Raft
  proto::UnmountRequest unmount_req;
  unmount_req.set_path("/tmp_mnt");
  proto::UnmountResponse unmount_resp;
  auto status = impl_->Unmount(&ctx, &unmount_req, &unmount_resp);
  ASSERT_TRUE(status.ok()) << status.error_message();

  proto::ListMountsRequest list_req;
  proto::ListMountsResponse list_resp;
  ASSERT_TRUE(impl_->ListMounts(&ctx, &list_req, &list_resp).ok());
  EXPECT_EQ(list_resp.paths_size(), 0);
}

TEST_F(RaftWritePathTest, MissingUnmountReturnsNotFound) {
  ::grpc::ServerContext ctx;

  proto::UnmountRequest req;
  req.set_path("/missing");
  proto::UnmountResponse resp;
  auto status = impl_->Unmount(&ctx, &req, &resp);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::NOT_FOUND);
}

TEST_F(RaftWritePathTest, CreateFileCompleteDeleteThroughRaft) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("existing.txt", 100, 0);
  RegisterFakeUfsForTest("raft-wp-test", std::move(fake));

  ::grpc::ServerContext ctx;

  // Mount
  proto::MountRequest mount_req;
  mount_req.set_path("/mnt");
  mount_req.set_ufs_uri("fake://raft-wp-test");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl_->Mount(&ctx, &mount_req, &mount_resp).ok());

  // CreateFile goes through Raft
  proto::CreateFileRequest create_req;
  create_req.set_path("/mnt/newfile.dat");
  proto::CreateFileResponse create_resp;
  auto create_status = impl_->CreateFile(&ctx, &create_req, &create_resp);
  ASSERT_TRUE(create_status.ok()) << create_status.error_message();
  EXPECT_GT(create_resp.file_info().inode_id(), 0u);
  EXPECT_EQ(create_resp.file_info().size(), 0u);
  EXPECT_FALSE(create_resp.file_info().is_directory());
  EXPECT_EQ(create_resp.ufs_uri(), "fake://raft-wp-test");
  EXPECT_EQ(create_resp.ufs_path(), "newfile.dat");

  uint64_t inode_id = create_resp.file_info().inode_id();

  // Verify the file is visible in InodeTree
  auto lookup = tree_->LookupPath("/mnt/newfile.dat");
  ASSERT_TRUE(lookup.has_value());
  EXPECT_EQ(*lookup, inode_id);
  auto created_entry = tree_->GetInode(inode_id);
  ASSERT_TRUE(created_entry.has_value());
  EXPECT_GT(created_entry->creation_time_ms, 0);

  // CompleteFile goes through Raft
  proto::CompleteFileRequest complete_req;
  complete_req.set_inode_id(inode_id);
  complete_req.set_size(4096);
  complete_req.set_ufs_mtime_ms(9999);
  proto::CompleteFileResponse complete_resp;
  auto complete_status =
      impl_->CompleteFile(&ctx, &complete_req, &complete_resp);
  ASSERT_TRUE(complete_status.ok()) << complete_status.error_message();

  // Verify size & mtime updated
  auto entry = tree_->GetInode(inode_id);
  ASSERT_TRUE(entry.has_value());
  EXPECT_EQ(entry->size, 4096u);
  EXPECT_EQ(entry->modification_time_ms, 9999);

  // GetFileInfo (read path, always local)
  proto::GetFileInfoRequest fi_req;
  fi_req.set_path("/mnt/newfile.dat");
  proto::GetFileInfoResponse fi_resp;
  auto fi_status = impl_->GetFileInfo(&ctx, &fi_req, &fi_resp);
  ASSERT_TRUE(fi_status.ok()) << fi_status.error_message();
  EXPECT_EQ(fi_resp.file_info().size(), 4096u);
  EXPECT_EQ(fi_resp.file_info().ufs_mtime_ms(), 9999);

  // DeleteFile goes through Raft
  proto::DeleteFileRequest delete_req;
  delete_req.set_path("/mnt/newfile.dat");
  proto::DeleteFileResponse delete_resp;
  auto delete_status = impl_->DeleteFile(&ctx, &delete_req, &delete_resp);
  ASSERT_TRUE(delete_status.ok()) << delete_status.error_message();

  // Verify deleted
  EXPECT_FALSE(tree_->LookupPath("/mnt/newfile.dat").has_value());
}

TEST_F(RaftWritePathTest, CreateFileDuplicateReturnsAlreadyExists) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("x.txt", 1, 0);
  RegisterFakeUfsForTest("raft-wp-dup", std::move(fake));

  ::grpc::ServerContext ctx;

  proto::MountRequest mount_req;
  mount_req.set_path("/dup");
  mount_req.set_ufs_uri("fake://raft-wp-dup");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl_->Mount(&ctx, &mount_req, &mount_resp).ok());

  proto::CreateFileRequest req;
  req.set_path("/dup/unique.dat");
  proto::CreateFileResponse resp;
  ASSERT_TRUE(impl_->CreateFile(&ctx, &req, &resp).ok());

  // Second create should fail
  proto::CreateFileResponse resp2;
  auto status2 = impl_->CreateFile(&ctx, &req, &resp2);
  EXPECT_FALSE(status2.ok());
  EXPECT_EQ(status2.error_code(), ::grpc::StatusCode::ALREADY_EXISTS);
}

TEST_F(RaftWritePathTest, GetFileInfoDoesNotSyncFromUfsWhenRaftEnabled) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("remote-only.txt", 128, 456);
  RegisterFakeUfsForTest("raft-wp-readonly", std::move(fake));

  ::grpc::ServerContext ctx;

  proto::MountRequest mount_req;
  mount_req.set_path("/readonly");
  mount_req.set_ufs_uri("fake://raft-wp-readonly");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl_->Mount(&ctx, &mount_req, &mount_resp).ok());

  EXPECT_FALSE(tree_->LookupPath("/readonly/remote-only.txt").has_value());

  proto::GetFileInfoRequest req;
  req.set_path("/readonly/remote-only.txt");
  proto::GetFileInfoResponse resp;
  auto status = impl_->GetFileInfo(&ctx, &req, &resp);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::NOT_FOUND);
  EXPECT_FALSE(tree_->LookupPath("/readonly/remote-only.txt").has_value());
}

}  // namespace fluxcache
