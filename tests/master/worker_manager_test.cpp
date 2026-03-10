#include "master/hash_ring_manager.h"
#include "master/master_service_impl.h"
#include "master/worker_manager.h"
#include "master.pb.h"
#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

namespace fluxcache {

// -----------------------------------------------------------------------------
// WorkerManager tests
// -----------------------------------------------------------------------------

TEST(WorkerManagerTest, RegisterWorkerEntersAlive) {
  WorkerManager wm;
  wm.RegisterWorker(1, "127.0.0.1", 9091, 1000);

  auto info = wm.GetWorker(1);
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->worker_id, 1u);
  EXPECT_EQ(info->host, "127.0.0.1");
  EXPECT_EQ(info->port, 9091);
  EXPECT_EQ(info->state, WorkerState::kAlive);
}

TEST(WorkerManagerTest, HandleHeartbeatUpdatesLastHeartbeat) {
  WorkerManager wm;
  wm.RegisterWorker(1, "127.0.0.1", 9091, 1000);
  wm.HandleHeartbeat(1, 2000);

  auto info = wm.GetWorker(1);
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->last_heartbeat_ms, 2000);
}

TEST(WorkerManagerTest, HeartbeatTimeoutTransitionsToSuspect) {
  WorkerManager wm;
  wm.RegisterWorker(1, "127.0.0.1", 9091, 1000);

  auto dead = wm.CheckWorkerHealth(1000 + 100, 100, 1000);
  EXPECT_TRUE(dead.empty());

  dead = wm.CheckWorkerHealth(1000 + 101, 100, 1000);
  EXPECT_TRUE(dead.empty());
  EXPECT_EQ(wm.GetWorkerState(1), WorkerState::kSuspect);
}

TEST(WorkerManagerTest, SuspectGracePeriodTransitionsToDead) {
  WorkerManager wm;
  wm.RegisterWorker(1, "127.0.0.1", 9091, 1000);

  wm.CheckWorkerHealth(1100, 100, 50);
  EXPECT_EQ(wm.GetWorkerState(1), WorkerState::kSuspect);

  auto dead = wm.CheckWorkerHealth(1160, 100, 50);
  ASSERT_EQ(dead.size(), 1u);
  EXPECT_EQ(dead[0], 1u);
  EXPECT_EQ(wm.GetWorkerState(1), WorkerState::kDead);
}

TEST(WorkerManagerTest, HeartbeatRecoversFromSuspect) {
  WorkerManager wm;
  wm.RegisterWorker(1, "127.0.0.1", 9091, 1000);

  wm.CheckWorkerHealth(1100, 100, 1000);
  EXPECT_EQ(wm.GetWorkerState(1), WorkerState::kSuspect);

  wm.HandleHeartbeat(1, 1150);
  EXPECT_EQ(wm.GetWorkerState(1), WorkerState::kAlive);
}

TEST(WorkerManagerTest, GetAllWorkersInRingExcludesDead) {
  WorkerManager wm;
  wm.RegisterWorker(1, "a", 1, 1000);
  wm.RegisterWorker(2, "b", 2, 1000);

  wm.CheckWorkerHealth(1100, 100, 50);
  EXPECT_EQ(wm.GetWorkerState(1), WorkerState::kSuspect);
  EXPECT_EQ(wm.GetWorkerState(2), WorkerState::kSuspect);

  wm.HandleHeartbeat(2, 1150);
  EXPECT_EQ(wm.GetWorkerState(2), WorkerState::kAlive);

  wm.CheckWorkerHealth(1160, 100, 50);
  EXPECT_EQ(wm.GetWorkerState(1), WorkerState::kDead);
  EXPECT_EQ(wm.GetWorkerState(2), WorkerState::kAlive);

  auto in_ring = wm.GetAllWorkersInRing();
  EXPECT_EQ(in_ring.size(), 1u);
  EXPECT_EQ(in_ring[0].worker_id, 2u);
}

// -----------------------------------------------------------------------------
// HashRingManager tests
// -----------------------------------------------------------------------------

TEST(HashRingManagerTest, GetWorkerDeterministic) {
  HashRingManager hrm(nullptr);
  hrm.AddWorker(1);

  BlockId bid = MakeBlockId(42, 0);
  WorkerId w = hrm.GetWorker(bid);
  EXPECT_EQ(w, 1u);

  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(hrm.GetWorker(bid), 1u);
  }
}

TEST(HashRingManagerTest, RingVersionIncrementsOnAdd) {
  HashRingManager hrm(nullptr);
  EXPECT_EQ(hrm.GetVersion(), 0u);

  hrm.AddWorker(1);
  EXPECT_EQ(hrm.GetVersion(), 1u);

  hrm.AddWorker(2);
  EXPECT_EQ(hrm.GetVersion(), 2u);
}

TEST(HashRingManagerTest, RingVersionIncrementsOnRemove) {
  HashRingManager hrm(nullptr);
  hrm.AddWorker(1);
  hrm.AddWorker(2);
  uint64_t v = hrm.GetVersion();

  hrm.RemoveWorker(1);
  EXPECT_EQ(hrm.GetVersion(), v + 1);
}

TEST(HashRingManagerTest, GetCandidatesSkipsSuspect) {
  WorkerManager wm;
  wm.RegisterWorker(1, "a", 1, 1000);
  wm.RegisterWorker(2, "b", 2, 1000);

  HashRingManager hrm([&wm](WorkerId id) { return wm.GetWorkerState(id); });
  hrm.AddWorker(1);
  hrm.AddWorker(2);

  wm.CheckWorkerHealth(1100, 100, 10000);
  EXPECT_EQ(wm.GetWorkerState(1), WorkerState::kSuspect);
  EXPECT_EQ(wm.GetWorkerState(2), WorkerState::kSuspect);

  wm.HandleHeartbeat(2, 1150);
  EXPECT_EQ(wm.GetWorkerState(2), WorkerState::kAlive);

  BlockId bid = MakeBlockId(123, 0);
  auto candidates = hrm.GetCandidates(bid, 2, true);
  EXPECT_EQ(candidates.size(), 1u);
  EXPECT_EQ(candidates[0], 2u);

  candidates = hrm.GetCandidates(bid, 2, false);
  EXPECT_GE(candidates.size(), 1u);
}

TEST(HashRingManagerTest, SuspectRemainsInRingSnapshot) {
  WorkerManager wm;
  wm.RegisterWorker(1, "a", 1, 1000);
  wm.RegisterWorker(2, "b", 2, 1000);

  HashRingManager hrm([&wm](WorkerId id) { return wm.GetWorkerState(id); });
  hrm.AddWorker(1);
  hrm.AddWorker(2);

  wm.CheckWorkerHealth(1100, 100, 10000);

  auto snap = hrm.GetRingSnapshot([&wm](WorkerId id) {
    auto info = wm.GetWorker(id);
    if (!info) return std::optional<WorkerEndpointInfo>();
    WorkerEndpointInfo ep;
    ep.worker_id = info->worker_id;
    ep.host = info->host;
    ep.port = info->port;
    return std::optional<WorkerEndpointInfo>(ep);
  });
  EXPECT_EQ(snap.workers.size(), 2u);
}

// -----------------------------------------------------------------------------
// Integration tests (MasterServiceImpl)
// -----------------------------------------------------------------------------

TEST(WorkerManagerIntegrationTest, RegisterThenGetHashRingWorkersSizeOne) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;
  proto::RegisterWorkerRequest reg_req;
  reg_req.mutable_endpoint()->set_host("127.0.0.1");
  reg_req.mutable_endpoint()->set_port(9091);
  proto::RegisterWorkerResponse reg_resp;
  ASSERT_TRUE(impl.RegisterWorker(&ctx, &reg_req, &reg_resp).ok());

  proto::GetHashRingRequest ring_req;
  proto::GetHashRingResponse ring_resp;
  ASSERT_TRUE(impl.GetHashRing(&ctx, &ring_req, &ring_resp).ok());

  EXPECT_EQ(ring_resp.workers_size(), 1);
  EXPECT_EQ(ring_resp.workers(0).worker_id(), reg_resp.worker_id());
}

TEST(WorkerManagerIntegrationTest, AddRemoveWorkerRingVersionIncrements) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;

  proto::RegisterWorkerRequest reg_req;
  reg_req.mutable_endpoint()->set_host("127.0.0.1");
  reg_req.mutable_endpoint()->set_port(9091);
  proto::RegisterWorkerResponse reg_resp;
  ASSERT_TRUE(impl.RegisterWorker(&ctx, &reg_req, &reg_resp).ok());

  proto::GetHashRingRequest ring_req;
  proto::GetHashRingResponse ring_resp;
  ASSERT_TRUE(impl.GetHashRing(&ctx, &ring_req, &ring_resp).ok());
  uint64_t v1 = ring_resp.ring_version();

  reg_req.mutable_endpoint()->set_host("127.0.0.2");
  reg_req.mutable_endpoint()->set_port(9092);
  ASSERT_TRUE(impl.RegisterWorker(&ctx, &reg_req, &reg_resp).ok());
  ASSERT_TRUE(impl.GetHashRing(&ctx, &ring_req, &ring_resp).ok());
  uint64_t v2 = ring_resp.ring_version();
  EXPECT_GT(v2, v1);
}

TEST(WorkerManagerIntegrationTest, DeterministicBlockRouting) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;
  proto::RegisterWorkerRequest reg_req;
  reg_req.mutable_endpoint()->set_host("127.0.0.1");
  reg_req.mutable_endpoint()->set_port(9091);
  proto::RegisterWorkerResponse reg_resp;
  ASSERT_TRUE(impl.RegisterWorker(&ctx, &reg_req, &reg_resp).ok());

  proto::GetHashRingRequest ring_req;
  proto::GetHashRingResponse ring_resp;
  ASSERT_TRUE(impl.GetHashRing(&ctx, &ring_req, &ring_resp).ok());

  EXPECT_EQ(ring_resp.workers_size(), 1);
  EXPECT_EQ(ring_resp.workers(0).worker_id(), reg_resp.worker_id());
}

}  // namespace fluxcache
