// Multi-Worker integration test.
// Validates: hash ring routing across 2+ Workers, data distribution,
// read/write correctness, and Worker-down resilience.

#include "client/fluxcache_client.h"
#include "common/config/config.h"
#include "master/master_server.h"
#include "ufs/fake_ufs.h"
#include "ufs/ufs_factory.h"
#include "worker/worker_server.h"

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fluxcache {

class MultiWorkerTest : public ::testing::Test {
 protected:
  static constexpr uint16_t kMasterPort = 29800;
  static constexpr uint16_t kWorker1Port = 29801;
  static constexpr uint16_t kWorker2Port = 29802;
  static constexpr uint16_t kWorker3Port = 29803;
  static constexpr size_t kPageSize = 1024 * 1024;  // 1MB

  void SetUp() override {
    namespace fs = std::filesystem;
    base_dir_ = fs::temp_directory_path() / "fluxcache_multi_worker_test";
    auto unique = base_dir_ / std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(unique);
    db_path_ = (unique / "db").string();

    MasterConfig master_cfg;
    master_cfg.host = "127.0.0.1";
    master_cfg.port = kMasterPort;
    master_cfg.db_path = db_path_;
    master_server_ = std::make_unique<MasterServer>(master_cfg);
    ASSERT_TRUE(master_server_->Start()) << "Master failed to start";

    StartWorker(kWorker1Port, unique / "worker1");
    StartWorker(kWorker2Port, unique / "worker2");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  void StartWorker(uint16_t port, const std::filesystem::path& data_dir) {
    std::filesystem::create_directories(data_dir);
    WorkerConfig cfg;
    cfg.data_dir = data_dir.string();
    cfg.host = "127.0.0.1";
    cfg.port = port;
    auto server = std::make_unique<WorkerServer>(cfg);
    ASSERT_TRUE(server->Start()) << "Worker on port " << port << " failed";
    workers_.push_back(std::move(server));
    worker_ports_.push_back(port);
  }

  void TearDown() override {
    for (auto& w : workers_) {
      if (w) w->Shutdown();
    }
    workers_.clear();
    if (master_server_) master_server_->Shutdown();
    try {
      std::filesystem::remove_all(base_dir_);
    } catch (...) {}
  }

  ClientConfig MakeClientConfig() {
    ClientConfig cfg;
    cfg.master_host = "127.0.0.1";
    cfg.master_port = kMasterPort;
    cfg.page_size = kPageSize;
    cfg.local_cache_enabled = false;
    return cfg;
  }

  void SetupMountAndWorkers(const std::string& ufs_authority) {
    ClientConfig cfg = MakeClientConfig();
    FluxCacheClient client(cfg);
    auto* master = client.GetMasterClient();
    ASSERT_TRUE(master->Mount("/mnt", "fake://" + ufs_authority).ok());
    for (uint16_t port : worker_ports_) {
      ASSERT_TRUE(master->RegisterWorker("127.0.0.1", port).ok());
    }
  }

  std::filesystem::path base_dir_;
  std::string db_path_;
  std::unique_ptr<MasterServer> master_server_;
  std::vector<std::unique_ptr<WorkerServer>> workers_;
  std::vector<uint16_t> worker_ports_;
};

// TC1: Two Workers registered, HashRing returns 2 workers
TEST_F(MultiWorkerTest, TwoWorkersInHashRing) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("mw-ring", std::move(fake));
  SetupMountAndWorkers("mw-ring");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);
  ASSERT_TRUE(client.RefreshRing().ok());

  auto ring = client.GetMasterClient()->GetHashRing();
  ASSERT_TRUE(ring.ok()) << ring.status().message();
  EXPECT_EQ(ring.value().workers_size(), 2)
      << "HashRing should contain both workers";
}

// TC2: Write + Read across two Workers is correct
TEST_F(MultiWorkerTest, WriteReadWithTwoWorkers) {
  auto fake = std::make_unique<FakeUfs>();
  RegisterFakeUfsForTest("mw-rw", std::move(fake));
  SetupMountAndWorkers("mw-rw");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  const std::string data(512 * 1024, 'M');
  auto ws = client.Write("/mnt/multi.dat", 0, data);
  ASSERT_TRUE(ws.ok()) << ws.message();

  auto rr = client.Read("/mnt/multi.dat", 0, data.size());
  ASSERT_TRUE(rr.ok()) << rr.status().message();
  EXPECT_EQ(rr.value(), data);
}

// TC3: Multiple files written with 2 Workers, all readable
TEST_F(MultiWorkerTest, MultipleFilesDistributedCorrectly) {
  auto fake = std::make_unique<FakeUfs>();
  RegisterFakeUfsForTest("mw-dist", std::move(fake));
  SetupMountAndWorkers("mw-dist");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  constexpr int kNumFiles = 10;
  for (int i = 0; i < kNumFiles; ++i) {
    std::string path = "/mnt/file_" + std::to_string(i) + ".dat";
    std::string content(1024, static_cast<char>('A' + i));
    auto ws = client.Write(path, 0, content);
    ASSERT_TRUE(ws.ok()) << "Write " << path << ": " << ws.message();
  }

  for (int i = 0; i < kNumFiles; ++i) {
    std::string path = "/mnt/file_" + std::to_string(i) + ".dat";
    std::string expected(1024, static_cast<char>('A' + i));
    auto rr = client.Read(path, 0, expected.size());
    ASSERT_TRUE(rr.ok()) << "Read " << path << ": " << rr.status().message();
    EXPECT_EQ(rr.value(), expected) << "Content mismatch for " << path;
  }
}

// TC4: Large file spanning multiple blocks routes pages to Workers via HashRing
TEST_F(MultiWorkerTest, LargeFileCrossBlockWithTwoWorkers) {
  auto fake = std::make_unique<FakeUfs>();
  RegisterFakeUfsForTest("mw-large", std::move(fake));
  SetupMountAndWorkers("mw-large");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  constexpr size_t kBlockSize = 64ULL * 1024 * 1024;
  std::string block0_page(kPageSize, 'X');
  std::string block1_page(kPageSize, 'Y');

  auto ws1 = client.Write("/mnt/large.dat", 0, block0_page);
  ASSERT_TRUE(ws1.ok()) << ws1.message();

  auto ws2 = client.Write("/mnt/large.dat", kBlockSize, block1_page);
  ASSERT_TRUE(ws2.ok()) << ws2.message();

  auto r0 = client.Read("/mnt/large.dat", 0, kPageSize);
  ASSERT_TRUE(r0.ok()) << r0.status().message();
  EXPECT_EQ(r0.value(), block0_page);

  auto r1 = client.Read("/mnt/large.dat", kBlockSize, kPageSize);
  ASSERT_TRUE(r1.ok()) << r1.status().message();
  EXPECT_EQ(r1.value(), block1_page);
}

// TC5: Adding a third Worker, new writes still succeed
TEST_F(MultiWorkerTest, AddThirdWorkerAndWriteRead) {
  auto fake = std::make_unique<FakeUfs>();
  RegisterFakeUfsForTest("mw-add", std::move(fake));
  SetupMountAndWorkers("mw-add");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  const std::string data_before(1024, 'B');
  ASSERT_TRUE(client.Write("/mnt/before.dat", 0, data_before).ok());

  namespace fs = std::filesystem;
  auto w3_dir = base_dir_ /
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
      / "worker3";
  StartWorker(kWorker3Port, w3_dir);
  ASSERT_TRUE(client.GetMasterClient()
                  ->RegisterWorker("127.0.0.1", kWorker3Port)
                  .ok());

  ASSERT_TRUE(client.RefreshRing().ok());
  auto ring = client.GetMasterClient()->GetHashRing();
  ASSERT_TRUE(ring.ok());
  EXPECT_EQ(ring.value().workers_size(), 3);

  const std::string data_after(1024, 'A');
  ASSERT_TRUE(client.Write("/mnt/after.dat", 0, data_after).ok());

  auto rr = client.Read("/mnt/after.dat", 0, data_after.size());
  ASSERT_TRUE(rr.ok()) << rr.status().message();
  EXPECT_EQ(rr.value(), data_after);

  auto rr_before = client.Read("/mnt/before.dat", 0, data_before.size());
  ASSERT_TRUE(rr_before.ok()) << rr_before.status().message();
  EXPECT_EQ(rr_before.value(), data_before);
}

// TC6: Worker shutdown — reads for data on the downed Worker fall back to UFS
TEST_F(MultiWorkerTest, WorkerDownReadFallsBackToUfs) {
  auto fake = std::make_unique<FakeUfs>();
  RegisterFakeUfsForTest("mw-down", std::move(fake));
  SetupMountAndWorkers("mw-down");

  ClientConfig cfg = MakeClientConfig();
  cfg.retry_max_attempts = 1;
  cfg.circuit_breaker_enabled = false;
  FluxCacheClient client(cfg);

  constexpr int kNumFiles = 5;
  for (int i = 0; i < kNumFiles; ++i) {
    std::string path = "/mnt/down_" + std::to_string(i) + ".dat";
    std::string content(1024, static_cast<char>('0' + i));
    ASSERT_TRUE(client.Write(path, 0, content).ok());
  }

  workers_[1]->Shutdown();
  workers_[1].reset();

  int success_count = 0;
  int fail_count = 0;
  for (int i = 0; i < kNumFiles; ++i) {
    std::string path = "/mnt/down_" + std::to_string(i) + ".dat";
    std::string expected(1024, static_cast<char>('0' + i));
    auto rr = client.Read(path, 0, expected.size());
    if (rr.ok()) {
      EXPECT_EQ(rr.value(), expected);
      ++success_count;
    } else {
      ++fail_count;
    }
  }

  EXPECT_GT(success_count, 0)
      << "At least some files should still be readable (routed to live Worker or UFS fallback)";
}

// TC7: Consistent routing — same block always maps to the same Worker
TEST_F(MultiWorkerTest, ConsistentBlockRouting) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("mw-consistent", std::move(fake));
  SetupMountAndWorkers("mw-consistent");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);
  ASSERT_TRUE(client.RefreshRing().ok());

  BlockId test_block = MakeBlockId(42, 0);
  auto w1 = client.GetWorkerForBlock(test_block);
  ASSERT_TRUE(w1.ok()) << w1.status().message();

  for (int i = 0; i < 100; ++i) {
    auto w = client.GetWorkerForBlock(test_block);
    ASSERT_TRUE(w.ok());
    EXPECT_EQ(w.value(), w1.value())
        << "Block routing must be deterministic (iteration " << i << ")";
  }
}

// TC8: Different blocks may route to different Workers (distribution sanity)
TEST_F(MultiWorkerTest, BlocksDistributeAcrossWorkers) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("mw-distribute", std::move(fake));
  SetupMountAndWorkers("mw-distribute");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);
  ASSERT_TRUE(client.RefreshRing().ok());

  std::set<WorkerId> seen_workers;
  for (uint64_t inode = 1; inode <= 100; ++inode) {
    BlockId bid = MakeBlockId(inode, 0);
    auto w = client.GetWorkerForBlock(bid);
    if (w.ok()) seen_workers.insert(w.value());
  }

  EXPECT_GE(seen_workers.size(), 2u)
      << "With 100 different inodes, blocks should distribute across both Workers";
}

}  // namespace fluxcache
