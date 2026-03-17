#include "master/ha/raft_node.h"
#include "master/inode_tree.h"
#include "master/master_service_impl.h"
#include "master.pb.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <chrono>
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

}  // namespace

class RaftTopologyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const std::vector<int> ports = {19611, 19612, 19613};
    for (size_t i = 0; i < ports.size(); ++i) {
      db_paths_.push_back(TempDir("fluxcache_raft_topology_db_XXXXXX"));
      snap_paths_.push_back(TempDir("fluxcache_raft_topology_snap_XXXXXX"));
      state_paths_.push_back(TempDir("fluxcache_raft_topology_state_XXXXXX"));
      ASSERT_FALSE(db_paths_.back().empty());
      ASSERT_FALSE(snap_paths_.back().empty());
      ASSERT_FALSE(state_paths_.back().empty());

      auto tree = std::make_unique<InodeTree>(db_paths_.back());
      ASSERT_TRUE(tree->InitOrRecover());
      trees_.push_back(std::move(tree));

      auto impl = std::make_unique<MasterServiceImpl>(trees_.back().get(), nullptr);
      impl->BindMountTableStore(trees_.back()->store());
      impl->RecoverMountTable();
      impls_.push_back(std::move(impl));
    }

    peers_ = {
        {1, "localhost:19611"},
        {2, "localhost:19612"},
        {3, "localhost:19613"},
    };

    for (size_t i = 0; i < ports.size(); ++i) {
      std::vector<RaftPeerConfig> other_peers;
      for (size_t j = 0; j < peers_.size(); ++j) {
        if (j == i) continue;
        other_peers.push_back(peers_[j]);
      }

      nodes_.push_back(std::make_unique<RaftNode>(
          static_cast<int>(i + 1), peers_[i].endpoint, trees_[i].get(),
          impls_[i]->mount_table_ptr(), snap_paths_[i], state_paths_[i],
          impls_[i].get()));
      ASSERT_TRUE(nodes_.back()->Start(ports[i], other_peers, 50, 200, 400,
                                       10000, 5000));
      impls_[i]->SetRaftNode(nodes_.back().get());
    }
  }

  void TearDown() override {
    for (auto& node : nodes_) {
      if (node) node->Shutdown();
    }
    nodes_.clear();
    impls_.clear();
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

  bool WaitForWorkerCount(const std::vector<size_t>& node_ids, int expected_count) {
    ::grpc::ServerContext ctx;
    proto::GetHashRingRequest req;
    for (int attempt = 0; attempt < 100; ++attempt) {
      bool ready = true;
      for (size_t idx : node_ids) {
        proto::GetHashRingResponse resp;
        auto status = impls_[idx]->GetHashRing(&ctx, &req, &resp);
        if (!status.ok() || resp.workers_size() != expected_count) {
          ready = false;
          break;
        }
      }
      if (ready) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
  }

  std::vector<std::string> db_paths_;
  std::vector<std::string> snap_paths_;
  std::vector<std::string> state_paths_;
  std::vector<std::unique_ptr<InodeTree>> trees_;
  std::vector<std::unique_ptr<MasterServiceImpl>> impls_;
  std::vector<std::unique_ptr<RaftNode>> nodes_;
  std::vector<RaftPeerConfig> peers_;
};

TEST_F(RaftTopologyTest, FollowerRejectsRegisterAndLeaderReplicatesRing) {
  int leader_idx = WaitForLeader({0, 1, 2});
  ASSERT_NE(leader_idx, -1);
  size_t follower_idx = (leader_idx == 0) ? 1u : 0u;

  ::grpc::ServerContext ctx;
  proto::RegisterWorkerRequest req;
  req.mutable_endpoint()->set_host("127.0.0.1");
  req.mutable_endpoint()->set_port(21001);
  proto::RegisterWorkerResponse resp;

  auto follower_status = impls_[follower_idx]->RegisterWorker(&ctx, &req, &resp);
  ASSERT_FALSE(follower_status.ok());
  EXPECT_EQ(follower_status.error_code(), ::grpc::StatusCode::UNAVAILABLE);

  auto leader_status = impls_[leader_idx]->RegisterWorker(&ctx, &req, &resp);
  ASSERT_TRUE(leader_status.ok()) << leader_status.error_message();
  EXPECT_EQ(resp.worker_id(), 1u);

  ASSERT_TRUE(WaitForWorkerCount({0, 1, 2}, 1));

  proto::GetHashRingRequest ring_req;
  proto::GetHashRingResponse leader_ring;
  proto::GetHashRingResponse follower_ring;
  ASSERT_TRUE(impls_[leader_idx]->GetHashRing(&ctx, &ring_req, &leader_ring).ok());
  ASSERT_TRUE(impls_[follower_idx]->GetHashRing(&ctx, &ring_req, &follower_ring).ok());
  ASSERT_EQ(leader_ring.workers_size(), 1);
  ASSERT_EQ(follower_ring.workers_size(), 1);
  EXPECT_EQ(follower_ring.workers(0).worker_id(), resp.worker_id());
  EXPECT_EQ(follower_ring.ring_version(), leader_ring.ring_version());
}

TEST_F(RaftTopologyTest, FailoverPreservesTopologyAndNextWorkerId) {
  int leader_idx = WaitForLeader({0, 1, 2});
  ASSERT_NE(leader_idx, -1);

  ::grpc::ServerContext ctx;
  proto::RegisterWorkerRequest req1;
  req1.mutable_endpoint()->set_host("127.0.0.1");
  req1.mutable_endpoint()->set_port(21011);
  proto::RegisterWorkerResponse resp1;
  ASSERT_TRUE(impls_[leader_idx]->RegisterWorker(&ctx, &req1, &resp1).ok());
  ASSERT_EQ(resp1.worker_id(), 1u);
  ASSERT_TRUE(WaitForWorkerCount({0, 1, 2}, 1));

  nodes_[leader_idx]->Shutdown();
  nodes_[leader_idx].reset();

  std::vector<size_t> survivors;
  for (size_t i = 0; i < nodes_.size(); ++i) {
    if (nodes_[i]) survivors.push_back(i);
  }
  int new_leader_idx = WaitForLeader(survivors);
  ASSERT_NE(new_leader_idx, -1);

  proto::GetHashRingRequest ring_req;
  proto::GetHashRingResponse ring_resp;
  ASSERT_TRUE(impls_[new_leader_idx]->GetHashRing(&ctx, &ring_req, &ring_resp).ok());
  ASSERT_EQ(ring_resp.workers_size(), 1);
  EXPECT_EQ(ring_resp.workers(0).worker_id(), resp1.worker_id());

  proto::RegisterWorkerRequest req2;
  req2.mutable_endpoint()->set_host("127.0.0.1");
  req2.mutable_endpoint()->set_port(21012);
  proto::RegisterWorkerResponse resp2;
  ASSERT_TRUE(impls_[new_leader_idx]->RegisterWorker(&ctx, &req2, &resp2).ok());
  EXPECT_EQ(resp2.worker_id(), 2u);

  ASSERT_TRUE(WaitForWorkerCount(survivors, 2));
}

TEST_F(RaftTopologyTest, LeaderReplicatesDeadWorkerRemoval) {
  int leader_idx = WaitForLeader({0, 1, 2});
  ASSERT_NE(leader_idx, -1);

  ::grpc::ServerContext ctx;
  proto::RegisterWorkerRequest req;
  req.mutable_endpoint()->set_host("127.0.0.1");
  req.mutable_endpoint()->set_port(21021);
  proto::RegisterWorkerResponse resp;
  ASSERT_TRUE(impls_[leader_idx]->RegisterWorker(&ctx, &req, &resp).ok());
  ASSERT_TRUE(WaitForWorkerCount({0, 1, 2}, 1));

  const int64_t base_now_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  impls_[leader_idx]->CheckWorkerHealthAndUpdateRing(base_now_ms + 1000, 100, 50);
  impls_[leader_idx]->CheckWorkerHealthAndUpdateRing(base_now_ms + 2000, 100, 50);

  ASSERT_TRUE(WaitForWorkerCount({0, 1, 2}, 0));
}

TEST_F(RaftTopologyTest, LeaderReplicatesSuspectStateBeforeRemoval) {
  int leader_idx = WaitForLeader({0, 1, 2});
  ASSERT_NE(leader_idx, -1);

  ::grpc::ServerContext ctx;
  proto::RegisterWorkerRequest req;
  req.mutable_endpoint()->set_host("127.0.0.1");
  req.mutable_endpoint()->set_port(21031);
  proto::RegisterWorkerResponse resp;
  ASSERT_TRUE(impls_[leader_idx]->RegisterWorker(&ctx, &req, &resp).ok());
  ASSERT_TRUE(WaitForWorkerCount({0, 1, 2}, 1));

  const int64_t base_now_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  impls_[leader_idx]->CheckWorkerHealthAndUpdateRing(base_now_ms + 1000, 100, 500);

  for (int attempt = 0; attempt < 100; ++attempt) {
    bool all_suspect = true;
    for (const auto& impl : impls_) {
      auto info = impl->worker_manager_ptr()->GetWorker(resp.worker_id());
      if (!info.has_value() || info->state != WorkerState::kSuspect) {
        all_suspect = false;
        break;
      }
    }
    if (all_suspect) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  for (const auto& impl : impls_) {
    auto info = impl->worker_manager_ptr()->GetWorker(resp.worker_id());
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, WorkerState::kSuspect);
  }
  ASSERT_TRUE(WaitForWorkerCount({0, 1, 2}, 1));
}

}  // namespace fluxcache
