// P1-15D: Write path E2E verification.
// Validates: write-then-read returns new content, UFS persistence visible,
// UFS injection failure returns error with no pseudo-success, single/cross-page/cross-block boundaries.

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

class WritePathE2ETest : public ::testing::Test {
 protected:
  static constexpr uint16_t kMasterPort = 29702;
  static constexpr uint16_t kWorkerPort = 29703;
  static constexpr size_t kPageSize = 1024 * 1024;  // 1MB

  void SetUp() override {
    namespace fs = std::filesystem;
    auto base = fs::temp_directory_path() / "fluxcache_write_e2e_test";
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
          std::filesystem::temp_directory_path() / "fluxcache_write_e2e_test");
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

// AC1: 写入后通过读路径可以返回新内容
TEST_F(WritePathE2ETest, WriteThenReadReturnsNewContent) {
  auto fake = std::make_unique<FakeUfs>();
  RegisterFakeUfsForTest("e2e-write-read", std::move(fake));

  SetupMountAndWorker("e2e-write-read");

  const std::string kContent(512 * 1024, 'X');
  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  auto write_status = client.Write("/mnt/new.dat", 0, kContent);
  ASSERT_TRUE(write_status.ok()) << write_status.message();

  auto read_result = client.Read("/mnt/new.dat", 0, kContent.size());
  ASSERT_TRUE(read_result.ok()) << read_result.status().message();
  EXPECT_EQ(read_result.value(), kContent);
}

// AC2: 写入后直接读取 UFS 文件可看到持久化结果
TEST_F(WritePathE2ETest, WriteThenDirectUfsReadShowsPersistedContent) {
  auto fake = std::make_unique<FakeUfs>();
  RegisterFakeUfsForTest("e2e-ufs-persist", std::move(fake));

  SetupMountAndWorker("e2e-ufs-persist");

  const std::string kContent(100 * 1024, 'P');
  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  auto write_status = client.Write("/mnt/persist.dat", 0, kContent);
  ASSERT_TRUE(write_status.ok()) << write_status.message();

  std::unique_ptr<UFS> ufs;
  ASSERT_TRUE(CreateUFS("fake", "e2e-ufs-persist", &ufs).ok());
  std::string ufs_content;
  ASSERT_TRUE(ufs->Read("persist.dat", 0, kContent.size(), &ufs_content).ok());
  EXPECT_EQ(ufs_content, kContent);
}

// AC3: UFS 注入失败时，E2E 返回错误且不留下伪成功状态
TEST_F(WritePathE2ETest, UfsWriteFailureReturnsErrorNoPseudoSuccess) {
  auto fake = std::make_unique<FakeUfs>();
  fake->SetWriteFail(true);
  RegisterFakeUfsForTest("e2e-ufs-fail", std::move(fake));

  SetupMountAndWorker("e2e-ufs-fail");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  const std::string kContent(100, 'Z');
  auto write_status = client.Write("/mnt/fail.dat", 0, kContent);
  EXPECT_FALSE(write_status.ok());

  auto fi = client.GetMasterClient()->GetFileInfo("/mnt/fail.dat");
  ASSERT_TRUE(fi.ok()) << fi.status().message();
  EXPECT_EQ(fi.value().file_info().size(), 0u)
      << "CompleteFile should not have been called on WritePages failure";
}

// AC4a: 单页边界
TEST_F(WritePathE2ETest, SinglePageBoundary) {
  auto fake = std::make_unique<FakeUfs>();
  RegisterFakeUfsForTest("e2e-single", std::move(fake));

  SetupMountAndWorker("e2e-single");

  const std::string kContent(kPageSize, 'A');
  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  ASSERT_TRUE(client.Write("/mnt/single.dat", 0, kContent).ok());

  auto read_result = client.Read("/mnt/single.dat", 0, kContent.size());
  ASSERT_TRUE(read_result.ok()) << read_result.status().message();
  EXPECT_EQ(read_result.value(), kContent);
}

// AC4b: 跨页边界
TEST_F(WritePathE2ETest, CrossPageBoundary) {
  auto fake = std::make_unique<FakeUfs>();
  RegisterFakeUfsForTest("e2e-cross-page", std::move(fake));

  SetupMountAndWorker("e2e-cross-page");

  std::string p0(kPageSize, '0');
  std::string p1(kPageSize, '1');
  std::string to_write = p0 + p1;

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  ASSERT_TRUE(client.Write("/mnt/cross_page.dat", 0, to_write).ok());

  auto read_result = client.Read("/mnt/cross_page.dat", 0, to_write.size());
  ASSERT_TRUE(read_result.ok()) << read_result.status().message();
  EXPECT_EQ(read_result.value(), to_write);
}

// AC4c: 跨 block 边界
TEST_F(WritePathE2ETest, CrossBlockBoundary) {
  constexpr size_t kBlockSize = 64ULL * 1024 * 1024;  // 64MB
  auto fake = std::make_unique<FakeUfs>();
  RegisterFakeUfsForTest("e2e-cross-block", std::move(fake));

  SetupMountAndWorker("e2e-cross-block");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  std::string block0_tail(kPageSize, 'A');
  std::string block1_head(kPageSize, 'B');

  auto s1 = client.Write("/mnt/cross_block.dat", kBlockSize - kPageSize,
                         block0_tail);
  ASSERT_TRUE(s1.ok()) << s1.message();

  auto s2 = client.Write("/mnt/cross_block.dat", kBlockSize, block1_head);
  ASSERT_TRUE(s2.ok()) << s2.message();

  auto read0 = client.Read("/mnt/cross_block.dat", kBlockSize - kPageSize,
                            kPageSize);
  ASSERT_TRUE(read0.ok()) << read0.status().message();
  EXPECT_EQ(read0.value(), block0_tail);

  auto read1 = client.Read("/mnt/cross_block.dat", kBlockSize, kPageSize);
  ASSERT_TRUE(read1.ok()) << read1.status().message();
  EXPECT_EQ(read1.value(), block1_head);
}

}  // namespace fluxcache
