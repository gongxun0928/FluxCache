#include "master/ha/raft_node.h"
#include "master/inode_tree.h"
#include "master/mount_table.h"
#include "master.pb.h"
#include <filesystem>
#include <gtest/gtest.h>
#include <cstdlib>
#include <vector>
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

class RaftNodeSingleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_path_ = TempDir("fluxcache_raftnode_test_XXXXXX");
    snap_path_ = TempDir("fluxcache_raftnode_snap_XXXXXX");
    raft_state_path_ = TempDir("fluxcache_raftnode_state_XXXXXX");
    ASSERT_FALSE(db_path_.empty());
    ASSERT_FALSE(snap_path_.empty());
    ASSERT_FALSE(raft_state_path_.empty());

    tree_ = std::make_unique<InodeTree>(db_path_);
    ASSERT_TRUE(tree_->InitOrRecover());
    mount_.BindStore(tree_->store());

    node_ = std::make_unique<RaftNode>(1, "localhost:19001",
                                        tree_.get(), &mount_, snap_path_,
                                        raft_state_path_);
    ASSERT_TRUE(node_->Start(19001, {}, 50, 100, 200, 10000, 5000));

    // Wait for the single-node cluster to elect itself as leader.
    for (int i = 0; i < 50; ++i) {
      if (node_->IsLeader()) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_TRUE(node_->IsLeader())
        << "Single node should become leader quickly";
  }

  void TearDown() override {
    if (node_) node_->Shutdown();
    node_.reset();
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
  MountTable mount_;
  std::unique_ptr<RaftNode> node_;
};

TEST_F(RaftNodeSingleTest, SingleNodeLeader) {
  EXPECT_TRUE(node_->IsLeader());
  EXPECT_EQ(node_->GetLeaderId(), 1);
}

TEST_F(RaftNodeSingleTest, ReplicateCreateFile) {
  auto alloc = tree_->AllocateInodeId();
  auto parent_id = tree_->LookupPath("/");
  ASSERT_TRUE(parent_id.has_value());

  proto::JournalEntry je;
  auto* op = je.mutable_create_file();
  op->set_path("/replicated.txt");
  op->set_inode_id(alloc.id);
  op->set_parent_id(*parent_id);
  op->set_size(0);
  op->set_block_size(1024);
  op->set_creation_time_ms(900);
  op->set_mtime_ms(1000);
  op->set_next_id(alloc.next_id);

  std::string serialized;
  ASSERT_TRUE(je.SerializeToString(&serialized));
  auto result = node_->Replicate(serialized);
  ASSERT_NE(result, nullptr);

  auto id = tree_->LookupPath("/replicated.txt");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(*id, alloc.id);
}

TEST_F(RaftNodeSingleTest, ReplicateMount) {
  proto::JournalEntry je;
  auto* op = je.mutable_mount_op();
  op->set_path("/mnt/data");
  op->set_ufs_uri("local:///tmp/data");

  std::string serialized;
  ASSERT_TRUE(je.SerializeToString(&serialized));
  auto result = node_->Replicate(serialized);
  ASSERT_NE(result, nullptr);

  auto mounts = mount_.ListMounts();
  ASSERT_EQ(mounts.size(), 1u);
  EXPECT_EQ(mounts[0], "/mnt/data");
}

TEST_F(RaftNodeSingleTest, ReplicateMultipleOps) {
  auto root_id = tree_->LookupPath("/");
  ASSERT_TRUE(root_id.has_value());

  // Create dir
  {
    auto alloc = tree_->AllocateInodeId();
    proto::JournalEntry je;
    auto* op = je.mutable_create_directory();
    op->set_path("/mydir");
    op->set_inode_id(alloc.id);
    op->set_parent_id(*root_id);
    op->set_creation_time_ms(1100);
    op->set_modification_time_ms(1100);
    op->set_next_id(alloc.next_id);

    std::string s;
    je.SerializeToString(&s);
    ASSERT_NE(node_->Replicate(s), nullptr);
  }

  auto dir_id = tree_->LookupPath("/mydir");
  ASSERT_TRUE(dir_id.has_value());

  // Create file under dir
  {
    auto alloc = tree_->AllocateInodeId();
    proto::JournalEntry je;
    auto* op = je.mutable_create_file();
    op->set_path("/mydir/file.bin");
    op->set_inode_id(alloc.id);
    op->set_parent_id(*dir_id);
    op->set_size(500);
    op->set_block_size(1024);
    op->set_creation_time_ms(1200);
    op->set_mtime_ms(3000);
    op->set_next_id(alloc.next_id);

    std::string s;
    je.SerializeToString(&s);
    ASSERT_NE(node_->Replicate(s), nullptr);
  }

  auto file_id = tree_->LookupPath("/mydir/file.bin");
  ASSERT_TRUE(file_id.has_value());

  auto entry = tree_->GetInode(*file_id);
  ASSERT_TRUE(entry.has_value());
  EXPECT_EQ(entry->size, 500u);

  // Delete file
  {
    proto::JournalEntry je;
    auto* op = je.mutable_delete_file();
    op->set_inode_id(*file_id);
    op->set_path("/mydir/file.bin");

    std::string s;
    je.SerializeToString(&s);
    ASSERT_NE(node_->Replicate(s), nullptr);
  }

  EXPECT_FALSE(tree_->LookupPath("/mydir/file.bin").has_value());
}

TEST_F(RaftNodeSingleTest, RejectsNewWritesAfterPersistFailure) {
  auto parent_id = tree_->LookupPath("/");
  ASSERT_TRUE(parent_id.has_value());

  node_->GetStateManager()->FailNextLogWriteForTest(1);

  proto::JournalEntry first;
  auto alloc1 = tree_->AllocateInodeId();
  auto* create1 = first.mutable_create_file();
  create1->set_path("/first_after_failure.txt");
  create1->set_inode_id(alloc1.id);
  create1->set_parent_id(*parent_id);
  create1->set_size(0);
  create1->set_block_size(1024);
  create1->set_creation_time_ms(1300);
  create1->set_mtime_ms(1300);
  create1->set_next_id(alloc1.next_id);
  std::string s1;
  ASSERT_TRUE(first.SerializeToString(&s1));
  EXPECT_EQ(node_->Replicate(s1), nullptr);
  EXPECT_FALSE(node_->GetStateManager()->IsHealthy());
  EXPECT_FALSE(node_->IsLeader());

  proto::JournalEntry second;
  auto alloc2 = tree_->AllocateInodeId();
  auto* create2 = second.mutable_create_file();
  create2->set_path("/rejected.txt");
  create2->set_inode_id(alloc2.id);
  create2->set_parent_id(*parent_id);
  create2->set_size(0);
  create2->set_block_size(1024);
  create2->set_creation_time_ms(1400);
  create2->set_mtime_ms(1400);
  create2->set_next_id(alloc2.next_id);
  std::string s2;
  ASSERT_TRUE(second.SerializeToString(&s2));
  EXPECT_EQ(node_->Replicate(s2), nullptr);
  EXPECT_FALSE(tree_->LookupPath("/rejected.txt").has_value());
}

}  // namespace fluxcache
