#include "master/ha/raft_state_machine.h"
#include "master/inode_tree.h"
#include "master/master_service_impl.h"
#include "master/mount_table.h"
#include "master.pb.h"
#include <filesystem>
#include <gtest/gtest.h>
#include <cstdlib>
#include <vector>

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

nuraft::ptr<nuraft::buffer> MakeBuffer(const proto::JournalEntry& entry) {
  std::string serialized;
  entry.SerializeToString(&serialized);
  nuraft::ptr<nuraft::buffer> buf = nuraft::buffer::alloc(serialized.size());
  buf->put_raw(reinterpret_cast<const nuraft::byte*>(serialized.data()),
               serialized.size());
  buf->pos(0);
  return buf;
}

nuraft::ptr<nuraft::buffer> MakeSnapshotObject(const std::string& path,
                                               const std::string& bytes) {
  auto buf = nuraft::buffer::alloc(sizeof(uint32_t) + path.size() +
                                   sizeof(uint32_t) + bytes.size());
  nuraft::buffer_serializer bs(buf);
  bs.put_str(path);
  bs.put_bytes(bytes.data(), bytes.size());
  return buf;
}

}  // namespace

class RaftStateMachineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_path_ = TempDir("fluxcache_raft_sm_test_XXXXXX");
    snap_path_ = TempDir("fluxcache_raft_snap_test_XXXXXX");
    ASSERT_FALSE(db_path_.empty());
    ASSERT_FALSE(snap_path_.empty());

    tree_ = std::make_unique<InodeTree>(db_path_);
    ASSERT_TRUE(tree_->InitOrRecover());

    mount_table_ = std::make_unique<MountTable>();
    mount_table_->BindStore(tree_->store());

    service_ = std::make_unique<MasterServiceImpl>(tree_.get(), nullptr);
    sm_ = std::make_unique<RaftStateMachine>(tree_.get(), mount_table_.get(),
                                             snap_path_, service_.get());
  }

  void TearDown() override {
    sm_.reset();
    service_.reset();
    mount_table_.reset();
    tree_.reset();
    std::error_code ec;
    std::filesystem::remove_all(db_path_, ec);
    std::filesystem::remove_all(snap_path_, ec);
  }

  std::string db_path_;
  std::string snap_path_;
  std::unique_ptr<InodeTree> tree_;
  std::unique_ptr<MountTable> mount_table_;
  std::unique_ptr<MasterServiceImpl> service_;
  std::unique_ptr<RaftStateMachine> sm_;
};

TEST_F(RaftStateMachineTest, CommitCreateFile) {
  auto alloc = tree_->AllocateInodeId();
  auto parent_id = tree_->LookupPath("/");
  ASSERT_TRUE(parent_id.has_value());

  proto::JournalEntry je;
  auto* op = je.mutable_create_file();
  op->set_path("/test.txt");
  op->set_inode_id(alloc.id);
  op->set_parent_id(*parent_id);
  op->set_size(0);
  op->set_block_size(64 * 1024 * 1024);
  op->set_creation_time_ms(900);
  op->set_mtime_ms(1000);
  op->set_next_id(alloc.next_id);

  auto buf = MakeBuffer(je);
  auto result = sm_->commit(1, *buf);
  ASSERT_NE(result, nullptr);

  nuraft::buffer_serializer bs(result);
  EXPECT_EQ(bs.get_u8(), static_cast<uint8_t>(StatusCode::kOk));
  EXPECT_EQ(bs.get_u64(), alloc.id);

  EXPECT_EQ(sm_->last_commit_index(), 1);

  auto inode_id = tree_->LookupPath("/test.txt");
  ASSERT_TRUE(inode_id.has_value());
  EXPECT_EQ(*inode_id, alloc.id);

  auto entry = tree_->GetInode(*inode_id);
  ASSERT_TRUE(entry.has_value());
  EXPECT_EQ(entry->size, 0u);
  EXPECT_EQ(entry->block_size, 64u * 1024 * 1024);
  EXPECT_EQ(entry->creation_time_ms, 900);
}

TEST_F(RaftStateMachineTest, FailedCreateFileApplyLeavesNoPartialState) {
  auto alloc = tree_->AllocateInodeId();
  auto parent_id = tree_->LookupPath("/");
  ASSERT_TRUE(parent_id.has_value());

  tree_->store()->FailNextWriteForTest(1);

  proto::JournalEntry je;
  auto* op = je.mutable_create_file();
  op->set_path("/partial.txt");
  op->set_inode_id(alloc.id);
  op->set_parent_id(*parent_id);
  op->set_size(0);
  op->set_block_size(64 * 1024 * 1024);
  op->set_creation_time_ms(901);
  op->set_mtime_ms(1001);
  op->set_next_id(alloc.next_id);

  auto buf = MakeBuffer(je);
  auto result = sm_->commit(11, *buf);
  ASSERT_NE(result, nullptr);

  nuraft::buffer_serializer bs(result);
  EXPECT_EQ(bs.get_u8(), static_cast<uint8_t>(StatusCode::kIOError));
  EXPECT_FALSE(tree_->LookupPath("/partial.txt").has_value());
  EXPECT_FALSE(tree_->GetInode(alloc.id).has_value());
  EXPECT_EQ(tree_->ListDirectory(*parent_id).size(), 0u);
}

TEST_F(RaftStateMachineTest, CommitCompleteFile) {
  auto alloc = tree_->AllocateInodeId();
  auto parent_id = tree_->LookupPath("/");
  ASSERT_TRUE(parent_id.has_value());

  ASSERT_TRUE(tree_->ApplyCreateFile(alloc.id, "/f1.txt", *parent_id,
                                     0, 64 * 1024 * 1024, 900, 1000,
                                     alloc.next_id).ok());

  proto::JournalEntry je;
  auto* op = je.mutable_complete_file();
  op->set_inode_id(alloc.id);
  op->set_size(12345);
  op->set_mtime_ms(2000);

  auto buf = MakeBuffer(je);
  auto result = sm_->commit(2, *buf);
  ASSERT_NE(result, nullptr);

  auto entry = tree_->GetInode(alloc.id);
  ASSERT_TRUE(entry.has_value());
  EXPECT_EQ(entry->size, 12345u);
  EXPECT_EQ(entry->modification_time_ms, 2000);
}

TEST_F(RaftStateMachineTest, CommitDeleteFile) {
  auto alloc = tree_->AllocateInodeId();
  auto parent_id = tree_->LookupPath("/");
  ASSERT_TRUE(parent_id.has_value());

  ASSERT_TRUE(tree_->ApplyCreateFile(alloc.id, "/del.txt", *parent_id,
                                     0, 64 * 1024 * 1024, 900, 1000,
                                     alloc.next_id).ok());

  proto::JournalEntry je;
  auto* op = je.mutable_delete_file();
  op->set_inode_id(alloc.id);
  op->set_path("/del.txt");

  auto buf = MakeBuffer(je);
  auto result = sm_->commit(3, *buf);
  ASSERT_NE(result, nullptr);

  EXPECT_FALSE(tree_->LookupPath("/del.txt").has_value());
}

TEST_F(RaftStateMachineTest, FailedDeleteFileApplyLeavesOriginalState) {
  auto alloc = tree_->AllocateInodeId();
  auto parent_id = tree_->LookupPath("/");
  ASSERT_TRUE(parent_id.has_value());
  ASSERT_TRUE(tree_->ApplyCreateFile(alloc.id, "/keep_delete.txt", *parent_id,
                                     0, 64 * 1024 * 1024, 900, 1000,
                                     alloc.next_id).ok());

  tree_->store()->FailNextWriteForTest(1);

  proto::JournalEntry je;
  auto* op = je.mutable_delete_file();
  op->set_inode_id(alloc.id);
  op->set_path("/keep_delete.txt");

  auto buf = MakeBuffer(je);
  auto result = sm_->commit(12, *buf);
  ASSERT_NE(result, nullptr);

  nuraft::buffer_serializer bs(result);
  EXPECT_EQ(bs.get_u8(), static_cast<uint8_t>(StatusCode::kIOError));
  auto inode_id = tree_->LookupPath("/keep_delete.txt");
  ASSERT_TRUE(inode_id.has_value());
  EXPECT_EQ(*inode_id, alloc.id);
  EXPECT_TRUE(tree_->GetInode(alloc.id).has_value());
}

TEST_F(RaftStateMachineTest, CommitMountUnmount) {
  {
    proto::JournalEntry je;
    auto* op = je.mutable_mount_op();
    op->set_path("/mnt");
    op->set_ufs_uri("local:///tmp/data");

    auto buf = MakeBuffer(je);
    auto result = sm_->commit(4, *buf);
    ASSERT_NE(result, nullptr);

    auto mounts = mount_table_->ListMounts();
    ASSERT_EQ(mounts.size(), 1u);
    EXPECT_EQ(mounts[0], "/mnt");
  }

  {
    proto::JournalEntry je;
    auto* op = je.mutable_unmount_op();
    op->set_path("/mnt");

    auto buf = MakeBuffer(je);
    auto result = sm_->commit(5, *buf);
    ASSERT_NE(result, nullptr);

    auto mounts = mount_table_->ListMounts();
    EXPECT_TRUE(mounts.empty());
  }
}

TEST_F(RaftStateMachineTest, CommitMountDuplicateReturnsFailure) {
  proto::JournalEntry je;
  auto* op = je.mutable_mount_op();
  op->set_path("/mnt");
  op->set_ufs_uri("local:///tmp/data");

  auto first = MakeBuffer(je);
  auto first_result = sm_->commit(7, *first);
  ASSERT_NE(first_result, nullptr);

  auto second = MakeBuffer(je);
  auto second_result = sm_->commit(8, *second);
  ASSERT_NE(second_result, nullptr);
  nuraft::buffer_serializer bs(second_result);
  EXPECT_EQ(bs.get_u8(), static_cast<uint8_t>(StatusCode::kInvalidArgument));
}

TEST_F(RaftStateMachineTest, CommitUnmountMissingReturnsFailure) {
  proto::JournalEntry je;
  auto* op = je.mutable_unmount_op();
  op->set_path("/missing");

  auto buf = MakeBuffer(je);
  auto result = sm_->commit(9, *buf);
  ASSERT_NE(result, nullptr);
  nuraft::buffer_serializer bs(result);
  EXPECT_EQ(bs.get_u8(), static_cast<uint8_t>(StatusCode::kNotFound));
}

TEST_F(RaftStateMachineTest, CommitCreateDirectory) {
  auto alloc = tree_->AllocateInodeId();
  auto parent_id = tree_->LookupPath("/");
  ASSERT_TRUE(parent_id.has_value());

  proto::JournalEntry je;
  auto* op = je.mutable_create_directory();
  op->set_path("/subdir");
  op->set_inode_id(alloc.id);
  op->set_parent_id(*parent_id);
  op->set_creation_time_ms(1200);
  op->set_modification_time_ms(1200);
  op->set_next_id(alloc.next_id);

  auto buf = MakeBuffer(je);
  auto result = sm_->commit(6, *buf);
  ASSERT_NE(result, nullptr);

  auto inode_id = tree_->LookupPath("/subdir");
  ASSERT_TRUE(inode_id.has_value());
  EXPECT_EQ(*inode_id, alloc.id);

  auto entry = tree_->GetInode(*inode_id);
  ASSERT_TRUE(entry.has_value());
  EXPECT_TRUE(entry->is_directory());
  EXPECT_EQ(entry->creation_time_ms, 1200);
  EXPECT_EQ(entry->modification_time_ms, 1200);
}

TEST_F(RaftStateMachineTest, CreateSnapshotAndLastSnapshot) {
  EXPECT_EQ(sm_->last_snapshot(), nullptr);

  auto dummy_config = nuraft::cs_new<nuraft::cluster_config>();
  nuraft::ptr<nuraft::snapshot> snap =
      nuraft::cs_new<nuraft::snapshot>(nuraft::ulong(10), nuraft::ulong(1),
                                       dummy_config);

  nuraft::async_result<bool>::handler_type handler =
      [](bool& result, nuraft::ptr<std::exception>& err) {
        EXPECT_TRUE(result);
        EXPECT_EQ(err, nullptr);
      };

  sm_->create_snapshot(*snap, handler);

  auto last = sm_->last_snapshot();
  ASSERT_NE(last, nullptr);
  EXPECT_EQ(last->get_last_log_idx(), 10u);
}

TEST_F(RaftStateMachineTest, ApplySnapshotRestoresMetadataState) {
  auto root_id = tree_->LookupPath("/");
  ASSERT_TRUE(root_id.has_value());

  auto dir_alloc = tree_->AllocateInodeId();
  ASSERT_TRUE(tree_->ApplyCreateDirectory(dir_alloc.id, "/snapdir", *root_id,
                                          1100, 1100, dir_alloc.next_id).ok());
  auto dir_id = tree_->LookupPath("/snapdir");
  ASSERT_TRUE(dir_id.has_value());

  auto file_alloc = tree_->AllocateInodeId();
  ASSERT_TRUE(tree_->ApplyCreateFile(file_alloc.id, "/snapdir/file.txt", *dir_id,
                                     11, 1024, 1200, 1234,
                                     file_alloc.next_id).ok());
  ASSERT_TRUE(mount_table_->ApplyMount("/snapmnt", "local:///tmp/snap").ok());

  auto cfg = nuraft::cs_new<nuraft::cluster_config>();
  auto snap =
      nuraft::cs_new<nuraft::snapshot>(nuraft::ulong(20), nuraft::ulong(2), cfg);
  nuraft::async_result<bool>::handler_type handler =
      [](bool& result, nuraft::ptr<std::exception>& err) {
        EXPECT_TRUE(result);
        EXPECT_EQ(err, nullptr);
      };
  sm_->create_snapshot(*snap, handler);

  ASSERT_TRUE(tree_->ApplyDeleteInode(file_alloc.id).ok());
  ASSERT_TRUE(mount_table_->ApplyUnmount("/snapmnt").ok());
  EXPECT_FALSE(tree_->LookupPath("/snapdir/file.txt").has_value());
  EXPECT_TRUE(mount_table_->ListMounts().empty());

  auto last = sm_->last_snapshot();
  ASSERT_NE(last, nullptr);
  ASSERT_TRUE(sm_->apply_snapshot(*last));

  auto restored_file = tree_->LookupPath("/snapdir/file.txt");
  ASSERT_TRUE(restored_file.has_value());
  EXPECT_EQ(*restored_file, file_alloc.id);
  auto mounts = mount_table_->ListMounts();
  ASSERT_EQ(mounts.size(), 1u);
  EXPECT_EQ(mounts[0], "/snapmnt");
}

TEST_F(RaftStateMachineTest, IncompleteIncomingSnapshotIsNotVisible) {
  EXPECT_EQ(sm_->last_snapshot(), nullptr);

  auto cfg = nuraft::cs_new<nuraft::cluster_config>();
  auto snap =
      nuraft::cs_new<nuraft::snapshot>(nuraft::ulong(21), nuraft::ulong(2), cfg);
  snap->set_size(2);

  nuraft::ulong obj_id = 0;
  auto data = MakeSnapshotObject("MANIFEST", "partial");
  sm_->save_logical_snp_obj(*snap, obj_id, *data, true, false);

  EXPECT_EQ(sm_->last_snapshot(), nullptr);
}

TEST_F(RaftStateMachineTest, FailedApplySnapshotKeepsCurrentState) {
  auto root_id = tree_->LookupPath("/");
  ASSERT_TRUE(root_id.has_value());

  auto alloc = tree_->AllocateInodeId();
  ASSERT_TRUE(tree_->ApplyCreateFile(alloc.id, "/keep.txt", *root_id,
                                     7, 1024, 900, 1000, alloc.next_id).ok());

  auto cfg = nuraft::cs_new<nuraft::cluster_config>();
  auto snap =
      nuraft::cs_new<nuraft::snapshot>(nuraft::ulong(30), nuraft::ulong(2), cfg);
  nuraft::async_result<bool>::handler_type handler =
      [](bool& result, nuraft::ptr<std::exception>& err) {
        EXPECT_TRUE(result);
        EXPECT_EQ(err, nullptr);
      };
  sm_->create_snapshot(*snap, handler);

  ASSERT_TRUE(tree_->ApplyDeleteInode(alloc.id).ok());
  auto alloc2 = tree_->AllocateInodeId();
  ASSERT_TRUE(tree_->ApplyCreateFile(alloc2.id, "/current.txt", *root_id,
                                     9, 1024, 1900, 2000, alloc2.next_id).ok());
  ASSERT_TRUE(mount_table_->ApplyMount("/current", "local:///tmp/current").ok());

  std::error_code ec;
  std::filesystem::remove_all(std::filesystem::path(snap_path_) / "snap_30", ec);

  auto last = sm_->last_snapshot();
  ASSERT_NE(last, nullptr);
  EXPECT_FALSE(sm_->apply_snapshot(*last));

  EXPECT_TRUE(tree_->is_ready());
  EXPECT_FALSE(tree_->LookupPath("/keep.txt").has_value());
  EXPECT_TRUE(tree_->LookupPath("/current.txt").has_value());
  auto mounts = mount_table_->ListMounts();
  ASSERT_EQ(mounts.size(), 1u);
  EXPECT_EQ(mounts[0], "/current");
}

TEST_F(RaftStateMachineTest, RestartReloadsSnapshotCatalogFromDisk) {
  auto root_id = tree_->LookupPath("/");
  ASSERT_TRUE(root_id.has_value());

  auto alloc = tree_->AllocateInodeId();
  ASSERT_TRUE(tree_->ApplyCreateFile(alloc.id, "/before-restart.txt", *root_id,
                                     7, 1024, 900, 1000,
                                     alloc.next_id).ok());
  ASSERT_TRUE(service_->ApplyReplicatedWorkerRegistration(
                  1, "127.0.0.1", 21041, 1000, 2)
                  .ok());

  auto cfg = nuraft::cs_new<nuraft::cluster_config>();
  auto snap =
      nuraft::cs_new<nuraft::snapshot>(nuraft::ulong(31), nuraft::ulong(2), cfg);
  nuraft::async_result<bool>::handler_type handler =
      [](bool& result, nuraft::ptr<std::exception>& err) {
        EXPECT_TRUE(result);
        EXPECT_EQ(err, nullptr);
      };
  sm_->create_snapshot(*snap, handler);

  ASSERT_TRUE(tree_->ApplyDeleteInode(alloc.id).ok());
  ASSERT_TRUE(service_->ApplyReplicatedWorkerState(1, WorkerState::kDead, 1000, 1000)
                  .ok());

  sm_.reset();
  sm_ = std::make_unique<RaftStateMachine>(tree_.get(), mount_table_.get(),
                                           snap_path_, service_.get());

  auto last = sm_->last_snapshot();
  ASSERT_NE(last, nullptr);
  EXPECT_EQ(last->get_last_log_idx(), 31u);
  ASSERT_TRUE(sm_->apply_snapshot(*last));

  EXPECT_TRUE(tree_->LookupPath("/before-restart.txt").has_value());
  auto worker = service_->worker_manager_ptr()->GetWorker(1);
  ASSERT_TRUE(worker.has_value());
  EXPECT_EQ(worker->state, WorkerState::kAlive);
}

TEST_F(RaftStateMachineTest, RestartIgnoresIncomingSnapshotDirectories) {
  auto cfg = nuraft::cs_new<nuraft::cluster_config>();
  auto snap =
      nuraft::cs_new<nuraft::snapshot>(nuraft::ulong(41), nuraft::ulong(2), cfg);
  snap->set_size(1);

  nuraft::ulong obj_id = 0;
  auto data = MakeSnapshotObject("worker_topology.pb", "");
  sm_->save_logical_snp_obj(*snap, obj_id, *data, true, true);

  sm_.reset();
  sm_ = std::make_unique<RaftStateMachine>(tree_.get(), mount_table_.get(),
                                           snap_path_, service_.get());

  EXPECT_EQ(sm_->last_snapshot(), nullptr);
}

TEST_F(RaftStateMachineTest, MultipleOpsSequence) {
  auto root_id = tree_->LookupPath("/");
  ASSERT_TRUE(root_id.has_value());

  uint64_t log_idx = 1;

  // Create directory /data
  {
    auto alloc = tree_->AllocateInodeId();
    proto::JournalEntry je;
    auto* op = je.mutable_create_directory();
    op->set_path("/data");
    op->set_inode_id(alloc.id);
    op->set_parent_id(*root_id);
    op->set_creation_time_ms(2100);
    op->set_modification_time_ms(2100);
    op->set_next_id(alloc.next_id);
    auto buf = MakeBuffer(je);
    sm_->commit(log_idx++, *buf);
  }

  auto data_id = tree_->LookupPath("/data");
  ASSERT_TRUE(data_id.has_value());

  // Create file /data/file1.txt
  {
    auto alloc = tree_->AllocateInodeId();
    proto::JournalEntry je;
    auto* op = je.mutable_create_file();
    op->set_path("/data/file1.txt");
    op->set_inode_id(alloc.id);
    op->set_parent_id(*data_id);
    op->set_size(100);
    op->set_block_size(1024);
    op->set_creation_time_ms(2200);
    op->set_mtime_ms(5000);
    op->set_next_id(alloc.next_id);
    auto buf = MakeBuffer(je);
    sm_->commit(log_idx++, *buf);
  }

  auto file_id = tree_->LookupPath("/data/file1.txt");
  ASSERT_TRUE(file_id.has_value());

  // Complete file
  {
    proto::JournalEntry je;
    auto* op = je.mutable_complete_file();
    op->set_inode_id(*file_id);
    op->set_size(9999);
    op->set_mtime_ms(6000);
    auto buf = MakeBuffer(je);
    sm_->commit(log_idx++, *buf);
  }

  auto entry = tree_->GetInode(*file_id);
  ASSERT_TRUE(entry.has_value());
  EXPECT_EQ(entry->size, 9999u);
  EXPECT_EQ(entry->modification_time_ms, 6000);

  EXPECT_EQ(sm_->last_commit_index(), log_idx - 1);
}

}  // namespace fluxcache
