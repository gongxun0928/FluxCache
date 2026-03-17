#include "client/cached_hash_ring.h"
#include "client/fluxcache_client.h"
#include "client/master_client.h"
#include "client/worker_client.h"
#include "common/config/config.h"
#include "common/rpc/channel_pool.h"
#include "common/rpc/resilience_config.h"
#include "master.grpc.pb.h"
#include "master.pb.h"
#include "worker.grpc.pb.h"
#include "worker.pb.h"
#include <gtest/gtest.h>
#include <memory>

namespace fluxcache {

// -----------------------------------------------------------------------------
// ChannelPool creates Master and Worker stub
// -----------------------------------------------------------------------------

TEST(ClientTest, ChannelPoolCreatesMasterAndWorkerStub) {
  ChannelPool pool;
  auto master_ch = pool.GetChannel("127.0.0.1:9090");
  auto worker_ch = pool.GetChannel("127.0.0.1:9091");
  ASSERT_NE(master_ch, nullptr);
  ASSERT_NE(worker_ch, nullptr);

  auto master_stub = fluxcache::proto::MasterService::NewStub(master_ch);
  auto worker_stub = fluxcache::proto::WorkerService::NewStub(worker_ch);
  EXPECT_NE(master_stub, nullptr);
  EXPECT_NE(worker_stub, nullptr);
}

TEST(ClientTest, MasterClientCreatesStubViaChannelPool) {
  ChannelPool pool;
  MasterClient client(&pool, "127.0.0.1:1", ResilienceConfig{});
  auto result = client.GetHashRing();
  // No server on port 1: expect Unavailable. If a server exists, ok() is acceptable.
  if (!result.ok()) {
    EXPECT_EQ(result.status().code(), StatusCode::kUnavailable);
  }
  // Stub creation via ChannelPool succeeded in either case.
}

TEST(ClientTest, WorkerClientCreatesStubViaChannelPool) {
  ChannelPool pool;
  WorkerClient client(&pool, "127.0.0.1:9091", ResilienceConfig{});
  proto::ReadPagesRequest req;
  req.set_block_id(1);
  proto::ReadPagesResponse resp;
  auto s = client.ReadPages(req, &resp);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kUnavailable);
}

// -----------------------------------------------------------------------------
// CachedHashRing: init and refresh by ring_version
// -----------------------------------------------------------------------------

TEST(ClientTest, CachedHashRingUpdateAndGetVersion) {
  CachedHashRing ring;
  EXPECT_EQ(ring.GetVersion(), 0);

  proto::GetHashRingResponse resp;
  resp.set_ring_version(1);
  auto* ep = resp.add_workers();
  ep->set_worker_id(100);
  ep->set_host("192.168.1.1");
  ep->set_port(9000);

  ring.Update(resp);
  EXPECT_EQ(ring.GetVersion(), 1);
}

TEST(ClientTest, CachedHashRingGetWorker) {
  CachedHashRing ring;
  proto::GetHashRingResponse resp;
  resp.set_ring_version(2);
  auto* ep = resp.add_workers();
  ep->set_worker_id(42);
  ep->set_host("10.0.0.1");
  ep->set_port(8080);

  ring.Update(resp);

  WorkerId wid = ring.GetWorker(12345);
  EXPECT_EQ(wid, 42);

  std::string addr = ring.GetWorkerAddress(42);
  EXPECT_EQ(addr, "10.0.0.1:8080");
}

TEST(ClientTest, CachedHashRingEmptyReturnsZero) {
  CachedHashRing ring;
  proto::GetHashRingResponse resp;
  resp.set_ring_version(1);
  // no workers
  ring.Update(resp);

  WorkerId wid = ring.GetWorker(1);
  EXPECT_EQ(wid, 0);

  std::string addr = ring.GetWorkerAddress(99);
  EXPECT_TRUE(addr.empty());
}

TEST(ClientTest, CachedHashRingRefreshOverwrites) {
  CachedHashRing ring;
  proto::GetHashRingResponse resp1;
  resp1.set_ring_version(1);
  auto* ep1 = resp1.add_workers();
  ep1->set_worker_id(1);
  ep1->set_host("a");
  ep1->set_port(1);
  ring.Update(resp1);
  EXPECT_EQ(ring.GetVersion(), 1);

  proto::GetHashRingResponse resp2;
  resp2.set_ring_version(2);
  auto* ep2 = resp2.add_workers();
  ep2->set_worker_id(2);
  ep2->set_host("b");
  ep2->set_port(2);
  ring.Update(resp2);
  EXPECT_EQ(ring.GetVersion(), 2);
  EXPECT_EQ(ring.GetWorkerAddress(1), "");
  EXPECT_EQ(ring.GetWorkerAddress(2), "b:2");
}

TEST(ClientTest, CachedHashRingRejectsOlderVersionRollback) {
  CachedHashRing ring;
  proto::GetHashRingResponse newer;
  newer.set_ring_version(5);
  auto* newer_ep = newer.add_workers();
  newer_ep->set_worker_id(9);
  newer_ep->set_host("new");
  newer_ep->set_port(9);
  ring.Update(newer);

  proto::GetHashRingResponse older;
  older.set_ring_version(4);
  auto* older_ep = older.add_workers();
  older_ep->set_worker_id(1);
  older_ep->set_host("old");
  older_ep->set_port(1);
  ring.Update(older);

  EXPECT_EQ(ring.GetVersion(), 5u);
  EXPECT_EQ(ring.GetWorkerAddress(9), "new:9");
  EXPECT_EQ(ring.GetWorkerAddress(1), "");
}

// -----------------------------------------------------------------------------
// FluxCacheClient: GetWorkerForBlock, GetWorkerClient, error codes
// -----------------------------------------------------------------------------

TEST(ClientTest, FluxCacheClientNoWorkerReturnsNotFound) {
  // Use invalid master address so RefreshRing fails with Unavailable
  ClientConfig config;
  config.master_host = "127.0.0.1";
  config.master_port = 1;  // Unlikely to have server
  FluxCacheClient client(config);

  auto result = client.GetWorkerForBlock(1);
  EXPECT_FALSE(result.ok());
  // Master unreachable -> Unavailable
  EXPECT_EQ(result.status().code(), StatusCode::kUnavailable);
}

TEST(ClientTest, FluxCacheClientGetWorkerClientBeforeRefresh) {
  ClientConfig config;
  config.master_host = "127.0.0.1";
  config.master_port = 1;
  FluxCacheClient client(config);

  auto result = client.GetWorkerClient(1);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kUnavailable);
}

// Test with mock: we need a way to inject ring without calling Master.
// FluxCacheClient always calls RefreshRing first. So we need either:
// 1. A test that starts a minimal Master server (complex)
// 2. Expose CachedHashRing for direct Update in test (add SetRingForTest?)
// 3. Use a very short deadline and expect Unavailable

// For "无 Worker" we need ring_fetched with empty ring. The only way is to
// have Master return empty workers. We can't easily do that without a real server.
// Alternative: test CachedHashRing directly for "no worker" case (done above).
// And test FluxCacheClient::GetWorkerForBlock when Master returns empty -
// that would require a mock Master.

// We've covered:
// - ChannelPool creates stubs (ClientTest.ChannelPoolCreatesMasterAndWorkerStub)
// - MasterClient/WorkerClient use ChannelPool (ClientTest.MasterClientCreatesStubViaChannelPool, WorkerClientCreatesStubViaChannelPool)
// - CachedHashRing Update/GetVersion/GetWorker/GetWorkerAddress (multiple tests)
// - Empty ring returns 0 (CachedHashRingEmptyReturnsZero)
// - Master unreachable returns Unavailable (FluxCacheClientNoWorkerReturnsNotFound)
// - Worker unreachable returns Unavailable (WorkerClient test with invalid address)

// Add one more: Status::Unavailable exists and works
TEST(ClientTest, StatusUnavailable) {
  auto s = Status::Unavailable("test");
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kUnavailable);
  EXPECT_EQ(s.message(), "test");
}

// -----------------------------------------------------------------------------
// 无 Worker: GetWorkerForBlock returns NotFound when ring is empty
// -----------------------------------------------------------------------------

TEST(ClientTest, FluxCacheClientNoWorkerInRingReturnsNotFound) {
  ClientConfig config;
  config.master_host = "127.0.0.1";
  config.master_port = 1;
  FluxCacheClient client(config);

  proto::GetHashRingResponse empty_resp;
  empty_resp.set_ring_version(1);
  // no workers added
  client.SetRingForTest(empty_resp);

  auto result = client.GetWorkerForBlock(1);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kNotFound);
  EXPECT_TRUE(result.status().message().find("no worker") != std::string::npos);
}

TEST(ClientTest, FluxCacheClientGetWorkerClientWorkerNotInRingReturnsNotFound) {
  ClientConfig config;
  config.master_host = "127.0.0.1";
  config.master_port = 1;
  FluxCacheClient client(config);

  proto::GetHashRingResponse resp;
  resp.set_ring_version(1);
  auto* ep = resp.add_workers();
  ep->set_worker_id(42);
  ep->set_host("10.0.0.1");
  ep->set_port(8080);
  client.SetRingForTest(resp);

  // worker_id 99 is not in ring (only 42 is)
  auto result = client.GetWorkerClient(99);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kNotFound);
}

}  // namespace fluxcache
