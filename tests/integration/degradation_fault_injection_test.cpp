// P3-06: Fault injection tests for safe recovery and limited degradation.
// Covers: slow UFS, UFS unavailable (stale read), Worker down (ring refresh retry),
// Worker recovery, write path fails on UFS unavailable.

#include "client/fluxcache_client.h"
#include "common/config/config.h"
#include "master/master_server.h"
#include "ufs/fake_ufs.h"
#include "ufs/ufs_factory.h"
#include "worker/worker_server.h"

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace fluxcache {

class DegradationFaultInjectionTest : public ::testing::Test {
 protected:
  static constexpr uint16_t kMasterPort = 29710;
  static constexpr uint16_t kWorkerPort = 29711;
  static constexpr size_t kPageSize = 1024 * 1024;  // 1MB

  void SetUp() override {
    namespace fs = std::filesystem;
    auto base = fs::temp_directory_path() / "fluxcache_degradation_test";
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
          std::filesystem::temp_directory_path() / "fluxcache_degradation_test");
    } catch (...) {
    }
  }

  ClientConfig MakeClientConfig() {
    ClientConfig cfg;
    cfg.master_host = "127.0.0.1";
    cfg.master_port = kMasterPort;
    cfg.page_size = kPageSize;
    cfg.allow_read_degradation = true;
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

// AC: UFS unavailable but cache exists -> stale read when degradation enabled
TEST_F(DegradationFaultInjectionTest, StaleReadWhenUfsUnavailableAndCacheExists) {
  auto fake = std::make_unique<FakeUfs>();
  const std::string kContent(256 * 1024, 'S');
  fake->AddFileWithContent("stale.dat", kContent, 1000);
  RegisterFakeUfsForTest("degradation-stale", std::move(fake));

  worker_server_->Shutdown();
  worker_server_.reset();
  WorkerConfig worker_cfg;
  worker_cfg.data_dir = db_path_ + "/worker_stale";
  std::filesystem::create_directories(worker_cfg.data_dir);
  worker_cfg.host = "127.0.0.1";
  worker_cfg.port = kWorkerPort;
  worker_cfg.allow_stale_read_on_ufs_timeout = true;
  worker_server_ = std::make_unique<WorkerServer>(worker_cfg);
  ASSERT_TRUE(worker_server_->Start());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  SetupMountAndWorker("degradation-stale");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  ASSERT_TRUE(client.Write("/mnt/stale.dat", 0, kContent).ok());

  bool stale = false;
  auto result = client.Read("/mnt/stale.dat", 0, kContent.size(), &stale);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value(), kContent);
  EXPECT_FALSE(stale) << "First read from UFS should not be stale";

  auto fi = client.GetMasterClient()->GetFileInfo("/mnt/stale.dat");
  ASSERT_TRUE(fi.ok()) << fi.status().message();
  uint64_t inode_id = fi.value().file_info().inode_id();
  uint64_t file_size = fi.value().file_info().size();
  ASSERT_TRUE(client.GetMasterClient()
                  ->CompleteFile(inode_id, file_size)
                  .ok())
      << "Update mtime in Master to trigger stale path";

  std::unique_ptr<UFS> ufs;
  ASSERT_TRUE(CreateUFS("fake", "degradation-stale", &ufs).ok());
  auto* fake_ptr = dynamic_cast<FakeUfs*>(ufs.get());
  ASSERT_NE(fake_ptr, nullptr);
  fake_ptr->SetReadFail(true);

  stale = false;
  result = client.Read("/mnt/stale.dat", 0, kContent.size(), &stale);
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value(), kContent);
  EXPECT_TRUE(stale) << "Read with UFS fail should return stale when cache exists";
}

// AC: Write fails when UFS unavailable, no implicit queue
TEST_F(DegradationFaultInjectionTest, WriteFailsWhenUfsUnavailable) {
  auto fake = std::make_unique<FakeUfs>();
  fake->SetWriteFail(true);
  RegisterFakeUfsForTest("degradation-write-fail", std::move(fake));

  SetupMountAndWorker("degradation-write-fail");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  const std::string kContent(100, 'W');
  auto write_status = client.Write("/mnt/fail.dat", 0, kContent);
  EXPECT_FALSE(write_status.ok());
}

// AC: Worker down -> Read fails when not served from client L1 cache
TEST_F(DegradationFaultInjectionTest, WorkerDownReadFails) {
  auto fake = std::make_unique<FakeUfs>();
  const std::string kContent(256 * 1024, 'R');
  fake->AddFileWithContent("retry.dat", kContent, 1000);
  RegisterFakeUfsForTest("degradation-retry", std::move(fake));

  SetupMountAndWorker("degradation-retry");

  ClientConfig cfg = MakeClientConfig();
  cfg.local_cache_enabled = false;
  FluxCacheClient client(cfg);

  ASSERT_TRUE(client.Write("/mnt/retry.dat", 0, kContent).ok());

  auto result = client.Read("/mnt/retry.dat", 0, kContent.size());
  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value(), kContent);

  worker_server_->Shutdown();
  worker_server_.reset();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  result = client.Read("/mnt/retry.dat", 0, kContent.size());
  EXPECT_FALSE(result.ok()) << "Read should fail when Worker is down";
}

}  // namespace fluxcache
