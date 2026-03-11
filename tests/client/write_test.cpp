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

class ClientWriteTest : public ::testing::Test {
 protected:
  static constexpr uint16_t kMasterPort = 29610;
  static constexpr uint16_t kWorkerPort = 29611;
  static constexpr size_t kPageSize = 1024 * 1024;  // 1MB

  void SetUp() override {
    namespace fs = std::filesystem;
    auto base = fs::temp_directory_path() / "fluxcache_write_test";
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
          std::filesystem::temp_directory_path() / "fluxcache_write_test");
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

TEST_F(ClientWriteTest, SinglePageWriteThenReadReturnsCorrectData) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("write-single", std::move(fake));

  SetupMountAndWorker("write-single");

  const std::string kContent(512 * 1024, 'X');
  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  auto write_status = client.Write("/mnt/single.dat", 0, kContent);
  ASSERT_TRUE(write_status.ok()) << write_status.message();

  auto read_result = client.Read("/mnt/single.dat", 0, kContent.size());
  ASSERT_TRUE(read_result.ok()) << read_result.status().message();
  EXPECT_EQ(read_result.value(), kContent);
}

TEST_F(ClientWriteTest, CrossPageWriteThenReadReturnsCorrectConcatenation) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("write-cross-page", std::move(fake));

  SetupMountAndWorker("write-cross-page");

  std::string p0(kPageSize, '0');
  std::string p1(kPageSize, '1');
  std::string to_write = p0 + p1;

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  auto write_status = client.Write("/mnt/cross_page.dat", 0, to_write);
  ASSERT_TRUE(write_status.ok()) << write_status.message();

  auto read_result = client.Read("/mnt/cross_page.dat", 0, to_write.size());
  ASSERT_TRUE(read_result.ok()) << read_result.status().message();
  EXPECT_EQ(read_result.value().size(), to_write.size());
  EXPECT_EQ(read_result.value(), to_write);
}

TEST_F(ClientWriteTest, CrossBlockWriteThenReadReturnsCorrectData) {
  constexpr size_t kBlockSize = 64ULL * 1024 * 1024;  // 64MB
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("write-cross-block", std::move(fake));

  SetupMountAndWorker("write-cross-block");

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

TEST_F(ClientWriteTest, WriteFailureDoesNotCallCompleteFile) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  fake->SetWriteFail(true);
  RegisterFakeUfsForTest("write-fail", std::move(fake));

  SetupMountAndWorker("write-fail");

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

}  // namespace fluxcache
