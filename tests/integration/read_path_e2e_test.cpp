// P1-14D: Read path E2E verification.
// Validates: correct content, FakeUfs.read_count for cache hit/miss,
// nonexistent file returns clear error.

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

class ReadPathE2ETest : public ::testing::Test {
 protected:
  static constexpr uint16_t kMasterPort = 29700;
  static constexpr uint16_t kWorkerPort = 29701;
  static constexpr size_t kPageSize = 1024 * 1024;  // 1MB

  void SetUp() override {
    namespace fs = std::filesystem;
    auto base = fs::temp_directory_path() / "fluxcache_read_e2e_test";
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
          std::filesystem::temp_directory_path() / "fluxcache_read_e2e_test");
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

// AC1: 读取 UFS 中已有文件返回正确内容
TEST_F(ReadPathE2ETest, ReadExistingFileReturnsCorrectContent) {
  const std::string kContent(512 * 1024, 'A');
  constexpr int64_t kMtime = 1234567890;
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("file.dat", kContent, kMtime);
  RegisterFakeUfsForTest("e2e-content", std::move(fake));

  SetupMountAndWorker("e2e-content");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);
  auto result = client.Read("/mnt/file.dat", 0, kContent.size());

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value(), kContent);
}

// AC2 & AC3: 首次读取 read_count 增长，再次读取相同页且版本未变时 read_count 不再增长
TEST_F(ReadPathE2ETest, FirstReadIncreasesReadCountSecondReadDoesNot) {
  const std::string kContent(kPageSize, 'B');
  constexpr int64_t kMtime = 9999;
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("cache.dat", kContent, kMtime);
  RegisterFakeUfsForTest("e2e-cache", std::move(fake));

  SetupMountAndWorker("e2e-cache");

  std::unique_ptr<UFS> ufs;
  ASSERT_TRUE(CreateUFS("fake", "e2e-cache", &ufs).ok());
  FakeUfs* fake_ptr = dynamic_cast<FakeUfs*>(ufs.get());
  ASSERT_NE(fake_ptr, nullptr);
  EXPECT_EQ(fake_ptr->read_count(), 0);

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  auto result1 = client.Read("/mnt/cache.dat", 0, kContent.size());
  ASSERT_TRUE(result1.ok()) << result1.status().message();
  EXPECT_EQ(result1.value(), kContent);
  EXPECT_EQ(fake_ptr->read_count(), 1) << "First read should fetch from UFS";

  auto result2 = client.Read("/mnt/cache.dat", 0, kContent.size());
  ASSERT_TRUE(result2.ok()) << result2.status().message();
  EXPECT_EQ(result2.value(), kContent);
  EXPECT_EQ(fake_ptr->read_count(), 1)
      << "Second read (same page, same version) should hit cache";
}

// AC4: 文件不存在时返回明确错误
TEST_F(ReadPathE2ETest, NonexistentFileReturnsClearError) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("other.dat", 10, 0);
  RegisterFakeUfsForTest("e2e-nonexist", std::move(fake));

  SetupMountAndWorker("e2e-nonexist");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);
  auto result = client.Read("/mnt/nonexistent.dat", 0, 10);

  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kNotFound);
}

}  // namespace fluxcache
