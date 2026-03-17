#include "master/ha/raft_node.h"
#include "master/inode_tree.h"
#include "master/mount_table.h"
#include "master.pb.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <cstdlib>
#include <thread>
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

std::string MakeCreateFileEntry(uint64_t inode_id, uint64_t parent_id,
                                uint64_t next_id, const std::string& path) {
  proto::JournalEntry je;
  auto* op = je.mutable_create_file();
  op->set_path(path);
  op->set_inode_id(inode_id);
  op->set_parent_id(parent_id);
  op->set_size(0);
  op->set_block_size(1024);
  op->set_creation_time_ms(1111);
  op->set_mtime_ms(1234);
  op->set_next_id(next_id);
  std::string serialized;
  EXPECT_TRUE(je.SerializeToString(&serialized));
  return serialized;
}

}  // namespace

class RaftClusterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const std::vector<int> ports = {19111, 19112, 19113};
    for (size_t i = 0; i < ports.size(); ++i) {
      db_paths_.push_back(TempDir("fluxcache_raft_cluster_db_XXXXXX"));
      snap_paths_.push_back(TempDir("fluxcache_raft_cluster_snap_XXXXXX"));
      state_paths_.push_back(TempDir("fluxcache_raft_cluster_state_XXXXXX"));
      ASSERT_FALSE(db_paths_.back().empty());
      ASSERT_FALSE(snap_paths_.back().empty());
      ASSERT_FALSE(state_paths_.back().empty());

      auto tree = std::make_unique<InodeTree>(db_paths_.back());
      ASSERT_TRUE(tree->InitOrRecover());
      trees_.push_back(std::move(tree));

      mounts_.push_back(std::make_unique<MountTable>());
      mounts_.back()->BindStore(trees_.back()->store());
    }

    peers_ = {
        {1, "localhost:19111"},
        {2, "localhost:19112"},
        {3, "localhost:19113"},
    };

    for (size_t i = 0; i < ports.size(); ++i) {
      nodes_.push_back(std::make_unique<RaftNode>(
          static_cast<int>(i + 1), peers_[i].endpoint, trees_[i].get(),
          mounts_[i].get(), snap_paths_[i], state_paths_[i]));

      std::vector<RaftPeerConfig> other_peers;
      for (size_t j = 0; j < peers_.size(); ++j) {
        if (j == i) continue;
        other_peers.push_back(peers_[j]);
      }

      ASSERT_TRUE(nodes_.back()->Start(ports[i], other_peers, 50, 200, 400,
                                       10000, 5000));
    }
  }

  void TearDown() override {
    for (auto& node : nodes_) {
      if (node) node->Shutdown();
    }
    nodes_.clear();
    mounts_.clear();
    trees_.clear();
    std::error_code ec;
    for (const auto& path : db_paths_) std::filesystem::remove_all(path, ec);
    for (const auto& path : snap_paths_) std::filesystem::remove_all(path, ec);
    for (const auto& path : state_paths_) std::filesystem::remove_all(path, ec);
  }

  int WaitForLeader(const std::vector<size_t>& candidates) {
    for (int attempt = 0; attempt < 100; ++attempt) {
      int leader = -1;
      for (size_t idx : candidates) {
        if (nodes_[idx] && nodes_[idx]->IsLeader()) {
          if (leader != -1) {
            leader = -1;
            break;
          }
          leader = static_cast<int>(idx);
        }
      }
      if (leader != -1) return leader;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return -1;
  }

  bool WaitForPathOnNodes(const std::string& path,
                          const std::vector<size_t>& node_ids) {
    for (int attempt = 0; attempt < 100; ++attempt) {
      bool all_ready = true;
      for (size_t idx : node_ids) {
        if (!trees_[idx]->LookupPath(path).has_value()) {
          all_ready = false;
          break;
        }
      }
      if (all_ready) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
  }

  std::vector<std::string> db_paths_;
  std::vector<std::string> snap_paths_;
  std::vector<std::string> state_paths_;
  std::vector<std::unique_ptr<InodeTree>> trees_;
  std::vector<std::unique_ptr<MountTable>> mounts_;
  std::vector<std::unique_ptr<RaftNode>> nodes_;
  std::vector<RaftPeerConfig> peers_;
};

TEST_F(RaftClusterTest, ElectsLeaderAndContinuesAfterFailover) {
  int leader_idx = WaitForLeader({0, 1, 2});
  ASSERT_NE(leader_idx, -1);

  auto alloc1 = trees_[leader_idx]->AllocateInodeId();
  auto root_id = trees_[leader_idx]->LookupPath("/");
  ASSERT_TRUE(root_id.has_value());
  ASSERT_NE(nodes_[leader_idx]->Replicate(
                MakeCreateFileEntry(alloc1.id, *root_id, alloc1.next_id,
                                    "/first.txt")),
            nullptr);
  ASSERT_TRUE(WaitForPathOnNodes("/first.txt", {0, 1, 2}));
  for (size_t idx : {0u, 1u, 2u}) {
    auto inode_id = trees_[idx]->LookupPath("/first.txt");
    ASSERT_TRUE(inode_id.has_value());
    auto entry = trees_[idx]->GetInode(*inode_id);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->creation_time_ms, 1111);
    EXPECT_EQ(entry->modification_time_ms, 1234);
  }

  nodes_[leader_idx]->Shutdown();
  nodes_[leader_idx].reset();

  std::vector<size_t> survivors;
  for (size_t i = 0; i < nodes_.size(); ++i) {
    if (nodes_[i]) survivors.push_back(i);
  }
  int new_leader_idx = WaitForLeader(survivors);
  ASSERT_NE(new_leader_idx, -1);

  auto alloc2 = trees_[new_leader_idx]->AllocateInodeId();
  ASSERT_NE(nodes_[new_leader_idx]->Replicate(
                MakeCreateFileEntry(alloc2.id, *root_id, alloc2.next_id,
                                    "/second.txt")),
            nullptr);
  ASSERT_TRUE(WaitForPathOnNodes("/second.txt", survivors));
}

}  // namespace fluxcache
