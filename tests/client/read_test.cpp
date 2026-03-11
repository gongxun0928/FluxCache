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

class ClientReadTest : public ::testing::Test {
 protected:
  static constexpr uint16_t kMasterPort = 29600;
  static constexpr uint16_t kWorkerPort = 29601;
  static constexpr size_t kPageSize = 1024 * 1024;  // 1MB

  void SetUp() override {
    namespace fs = std::filesystem;
    auto base = fs::temp_directory_path() / "fluxcache_read_test";
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
          std::filesystem::temp_directory_path() / "fluxcache_read_test");
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

TEST_F(ClientReadTest, SinglePageReadReturnsCorrectData) {
  const std::string kContent(512 * 1024, 'A');
  constexpr int64_t kMtime = 1234567890;
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("single.dat", kContent, kMtime);
  RegisterFakeUfsForTest("read-single", std::move(fake));

  SetupMountAndWorker("read-single");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);
  auto result = client.Read("/mnt/single.dat", 0, kContent.size());

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value(), kContent);
}

TEST_F(ClientReadTest, CrossPageReadReturnsCorrectConcatenation) {
  std::string p0(kPageSize, '0');
  std::string p1(kPageSize, '1');
  std::string expected = p0 + p1;
  constexpr int64_t kMtime = 9999;
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("cross_page.dat", expected, kMtime);
  RegisterFakeUfsForTest("read-cross-page", std::move(fake));

  SetupMountAndWorker("read-cross-page");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);
  auto result = client.Read("/mnt/cross_page.dat", 0, expected.size());

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value().size(), expected.size());
  EXPECT_EQ(result.value(), expected);
}

TEST_F(ClientReadTest, PartialPageReadReturnsCorrectSlice) {
  std::string full(kPageSize * 2, 'X');
  for (size_t i = 0; i < kPageSize; ++i) full[i] = 'A';
  for (size_t i = kPageSize; i < full.size(); ++i) full[i] = 'B';
  constexpr int64_t kMtime = 1111;
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("partial.dat", full, kMtime);
  RegisterFakeUfsForTest("read-partial", std::move(fake));

  SetupMountAndWorker("read-partial");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);
  size_t offset = kPageSize / 2;
  size_t size = kPageSize;
  auto result = client.Read("/mnt/partial.dat", offset, size);

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value().size(), size);
  std::string expected(kPageSize / 2, 'A');
  expected += std::string(kPageSize / 2, 'B');
  EXPECT_EQ(result.value(), expected);
}

TEST_F(ClientReadTest, OffsetBeyondFileSizeReturnsEmpty) {
  const std::string kContent(100, 'Z');
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("small.dat", kContent, 0);
  RegisterFakeUfsForTest("read-empty", std::move(fake));

  SetupMountAndWorker("read-empty");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);
  auto result = client.Read("/mnt/small.dat", 200, 100);

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_TRUE(result.value().empty());
}

TEST_F(ClientReadTest, SizeClampedToFileEnd) {
  const std::string kContent(100, 'C');
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("clamp.dat", kContent, 0);
  RegisterFakeUfsForTest("read-clamp", std::move(fake));

  SetupMountAndWorker("read-clamp");

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);
  auto result = client.Read("/mnt/clamp.dat", 50, 1000);

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_EQ(result.value().size(), 50u);
  EXPECT_EQ(result.value(), std::string(50, 'C'));
}

TEST_F(ClientReadTest, NonexistentPathReturnsNotFound) {
  SetupMountAndWorker("read-nonexist");
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("other.dat", 10, 0);
  RegisterFakeUfsForTest("read-nonexist", std::move(fake));

  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);
  auto result = client.Read("/mnt/nonexistent.dat", 0, 10);

  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kNotFound);
}

}  // namespace fluxcache
