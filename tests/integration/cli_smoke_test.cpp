// P1-12: CLI smoke test. Validates write -> read -> stat on minimal cluster.

#include "client/fluxcache_client.h"
#include "common/config/config.h"
#include "master/master_server.h"
#include "ufs/fake_ufs.h"
#include "ufs/ufs_factory.h"
#include "worker/worker_server.h"

#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace fluxcache {

class CliSmokeTest : public ::testing::Test {
 protected:
  static constexpr uint16_t kMasterPort = 29710;
  static constexpr uint16_t kWorkerPort = 29711;
  static constexpr size_t kPageSize = 1024 * 1024;

  void SetUp() override {
    namespace fs = std::filesystem;
    auto base = fs::temp_directory_path() / "fluxcache_cli_smoke_test";
    auto unique =
        base / std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(unique);
    db_path_ = (unique / "db").string();
    config_path_ = (unique / "config.yaml").string();

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

    auto fake = std::make_unique<FakeUfs>();
    RegisterFakeUfsForTest("cli-smoke", std::move(fake));

    WriteConfig();
    SetupMountAndWorker();
  }

  void TearDown() override {
    if (worker_server_) worker_server_->Shutdown();
    if (master_server_) master_server_->Shutdown();
    try {
      std::filesystem::remove_all(
          std::filesystem::temp_directory_path() /
          "fluxcache_cli_smoke_test");
    } catch (...) {
    }
  }

  void WriteConfig() {
    std::ofstream ofs(config_path_);
    ofs << "fluxcache:\n"
        << "  master:\n"
        << "    host: \"127.0.0.1\"\n"
        << "    port: " << kMasterPort << "\n"
        << "  worker:\n"
        << "    data_dir: \"/tmp/fc\"\n"
        << "    host: \"0.0.0.0\"\n"
        << "    port: " << kWorkerPort << "\n"
        << "  client:\n"
        << "    master_host: \"127.0.0.1\"\n"
        << "    master_port: " << kMasterPort << "\n"
        << "  ufs:\n"
        << "    type: \"localfs\"\n"
        << "    path: \"/tmp/fc\"\n";
    ofs.close();
  }

  void SetupMountAndWorker() {
    ClientConfig cfg = MakeClientConfig();
    FluxCacheClient client(cfg);
    auto* master = client.GetMasterClient();
    ASSERT_TRUE(master->Mount("/mnt", "fake://cli-smoke").ok());
    ASSERT_TRUE(master->RegisterWorker("127.0.0.1", kWorkerPort).ok());
  }

  ClientConfig MakeClientConfig() {
    ClientConfig cfg;
    cfg.master_host = "127.0.0.1";
    cfg.master_port = kMasterPort;
    cfg.page_size = kPageSize;
    return cfg;
  }

  std::string db_path_;
  std::string config_path_;
  std::unique_ptr<MasterServer> master_server_;
  std::unique_ptr<WorkerServer> worker_server_;
};

// AC: write -> read -> stat verification
TEST_F(CliSmokeTest, WriteReadStatFlow) {
  ClientConfig cfg = MakeClientConfig();
  FluxCacheClient client(cfg);

  const std::string kContent("hello fluxcache cli smoke");
  auto write_s = client.Write("/mnt/smoke.dat", 0, kContent);
  ASSERT_TRUE(write_s.ok()) << write_s.message();

  auto read_result = client.Read("/mnt/smoke.dat", 0, kContent.size());
  ASSERT_TRUE(read_result.ok()) << read_result.status().message();
  EXPECT_EQ(read_result.value(), kContent);

  auto fi_result = client.GetMasterClient()->GetFileInfo("/mnt/smoke.dat");
  ASSERT_TRUE(fi_result.ok()) << fi_result.status().message();
  EXPECT_EQ(fi_result.value().file_info().size(), kContent.size());
}

// AC: cluster unreachable outputs clear error
TEST_F(CliSmokeTest, UnreachableClusterReturnsClearError) {
  ClientConfig cfg;
  cfg.master_host = "127.0.0.1";
  cfg.master_port = 39999;  // no server
  cfg.page_size = kPageSize;

  FluxCacheClient client(cfg);
  auto result = client.Read("/mnt/any.dat", 0, 10);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kUnavailable);
}

}  // namespace fluxcache
