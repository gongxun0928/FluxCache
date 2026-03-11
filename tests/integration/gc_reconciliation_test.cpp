// P2-05B: Worker GC and orphan/misplaced block reconciliation.
// Validates: DeleteFile + Heartbeat cleans orphan cache pages; reconciliation
// does not block normal read/write.

#include "client/fluxcache_client.h"
#include "common/config/config.h"
#include "common/types.h"
#include "master/master_server.h"
#include "master/master_service_impl.h"
#include "ufs/fake_ufs.h"
#include "ufs/ufs_factory.h"
#include "worker/worker_server.h"

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace fluxcache {

class GcReconciliationTest : public ::testing::Test {
 protected:
  static constexpr uint16_t kMasterPort = 29712;
  static constexpr uint16_t kWorkerPort = 29713;
  static constexpr size_t kPageSize = 1024 * 1024;  // 1MB

  void SetUp() override {
    namespace fs = std::filesystem;
    auto base = fs::temp_directory_path() / "fluxcache_gc_recon_test";
    auto unique = base / std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(unique);
    db_path_ = (unique / "db").string();

    MasterConfig master_cfg;
    master_cfg.host = "127.0.0.1";
    master_cfg.port = kMasterPort;
    master_cfg.db_path = db_path_;

    WorkerConfig worker_cfg;
    worker_cfg.data_dir = (unique / "worker").string();
    worker_cfg.host = "127.0.0.1";
    worker_cfg.port = kWorkerPort;

    master_server_ = std::make_unique<MasterServer>(master_cfg);
    worker_server_ = std::make_unique<WorkerServer>(worker_cfg);

    ASSERT_TRUE(master_server_->Start()) << "Master failed to start";
    ASSERT_TRUE(worker_server_->Start()) << "Worker failed to start";

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  void TearDown() override {
    if (worker_server_) worker_server_->Shutdown();
    if (master_server_) master_server_->Shutdown();
    try {
      std::filesystem::remove_all(
          std::filesystem::temp_directory_path() / "fluxcache_gc_recon_test");
    } catch (...) {
    }
  }

  ClientConfig MakeClientConfig() {
    ClientConfig cfg;
    cfg.master_host = "127.0.0.1";
    cfg.master_port = kMasterPort;
    cfg.page_size = kPageSize;
    return cfg;
  }

  void SetupMountAndWorker(const std::string& ufs_authority) {
    ClientConfig cfg = MakeClientConfig();
    FluxCacheClient client(cfg);
    auto* master = client.GetMasterClient();
    ASSERT_TRUE(master->Mount("/mnt", "fake://" + ufs_authority).ok());
    ASSERT_TRUE(master->RegisterWorker("127.0.0.1", kWorkerPort).ok());
  }

  std::string db_path_;
  std::unique_ptr<MasterServer> master_server_;
  std::unique_ptr<WorkerServer> worker_server_;
};

// AC1: Delete file after write, Heartbeat triggers GC, cache pages are cleaned.
TEST_F(GcReconciliationTest, OrphanCleanupAfterDeleteFile) {
  auto fake = std::make_unique<FakeUfs>();
  RegisterFakeUfsForTest("gc-orphan", std::move(fake));

  SetupMountAndWorker("gc-orphan");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  const std::string kContent(kPageSize, 'X');
  ASSERT_TRUE(client.Write("/mnt/orphan.dat", 0, kContent).ok());

  auto fi = client.GetMasterClient()->GetFileInfo("/mnt/orphan.dat");
  ASSERT_TRUE(fi.ok()) << fi.status().message();
  uint64_t inode_id = fi.value().file_info().inode_id();
  BlockId block_id = MakeBlockId(inode_id, 0);

  ASSERT_TRUE(worker_server_->page_store()->ContainsBlock(block_id))
      << "Block should be cached before delete";

  ASSERT_TRUE(client.Delete("/mnt/orphan.dat").ok());

  master_server_->service_impl()->RunHeartbeatToAllWorkers();

  EXPECT_FALSE(worker_server_->page_store()->ContainsBlock(block_id))
      << "Orphan block should be cleaned after Heartbeat reconciliation";
}

// AC2: Reconciliation does not block normal read/write.
TEST_F(GcReconciliationTest, ReconciliationDoesNotBlockReadWrite) {
  auto fake = std::make_unique<FakeUfs>();
  RegisterFakeUfsForTest("gc-nonblock", std::move(fake));

  SetupMountAndWorker("gc-nonblock");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  const std::string kContent(kPageSize, 'A');
  ASSERT_TRUE(client.Write("/mnt/active.dat", 0, kContent).ok());

  auto read_result = client.Read("/mnt/active.dat", 0, kContent.size());
  ASSERT_TRUE(read_result.ok()) << read_result.status().message();
  EXPECT_EQ(read_result.value(), kContent);

  master_server_->service_impl()->RunHeartbeatToAllWorkers();

  read_result = client.Read("/mnt/active.dat", 0, kContent.size());
  ASSERT_TRUE(read_result.ok()) << read_result.status().message();
  EXPECT_EQ(read_result.value(), kContent)
      << "Read should still succeed after Heartbeat";
}

}  // namespace fluxcache
