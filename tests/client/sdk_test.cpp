// P2-09: C++ SDK MVP tests.
// Uses only SDK public API for Create/Open/Read/Write/Stat/Delete.
// Setup uses FluxCacheClient for Mount/RegisterWorker (test infrastructure).

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#include "client/fluxcache_client.h"
#include "client/sdk/fluxcache_sdk.h"
#include "client/sdk/types.h"
#include "common/config/config.h"
#include "master/master_server.h"
#include "ufs/fake_ufs.h"
#include "ufs/ufs_factory.h"
#include "worker/worker_server.h"

namespace fluxcache {

class SdkTest : public ::testing::Test {
 protected:
  static constexpr uint16_t kMasterPort = 29620;
  static constexpr uint16_t kWorkerPort = 29621;
  static constexpr size_t kPageSize = 1024 * 1024;  // 1MB

  void SetUp() override {
    namespace fs = std::filesystem;
    auto base = fs::temp_directory_path() / "fluxcache_sdk_test";
    auto unique =
        base / std::to_string(
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
      std::filesystem::remove_all(std::filesystem::temp_directory_path() /
                                  "fluxcache_sdk_test");
    } catch (...) {
    }
  }

  fluxcache::SDKConfig MakeSdkConfig() {
    fluxcache::SDKConfig cfg;
    cfg.master_host = "127.0.0.1";
    cfg.master_port = kMasterPort;
    cfg.page_size = kPageSize;
    return cfg;
  }

  void SetupMountAndWorker(const std::string &ufs_authority) {
    ClientConfig cfg;
    cfg.master_host = "127.0.0.1";
    cfg.master_port = kMasterPort;
    cfg.page_size = kPageSize;
    FluxCacheClient client(cfg);
    auto *master = client.GetMasterClient();
    ASSERT_TRUE(master->Mount("/mnt", "fake://" + ufs_authority).ok());
    ASSERT_TRUE(master->RegisterWorker("127.0.0.1", kWorkerPort).ok());
  }

  std::string db_path_;
  std::unique_ptr<MasterServer> master_server_;
  std::unique_ptr<WorkerServer> worker_server_;
};

TEST_F(SdkTest, CreateOpenWriteReadStat) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("sdk-main", std::move(fake));

  SetupMountAndWorker("sdk-main");

  auto sdk_result = FluxCacheSDK::Create(MakeSdkConfig());
  ASSERT_TRUE(sdk_result.ok()) << sdk_result.status().message();
  auto sdk = std::move(sdk_result.value());

  const std::string path = "/mnt/sdk_test.dat";
  const std::string data = "Hello, SDK!";

  // Create
  auto create_status = sdk->Create(path);
  ASSERT_TRUE(create_status.ok()) << create_status.message();

  // Open
  auto open_result = sdk->Open(path, OpenMode::kReadWrite);
  ASSERT_TRUE(open_result.ok()) << open_result.status().message();
  auto handle = std::move(open_result.value());

  // Write
  auto write_status = handle->Write(0, data);
  ASSERT_TRUE(write_status.ok()) << write_status.message();

  // Stat
  auto stat_result = handle->Stat();
  ASSERT_TRUE(stat_result.ok()) << stat_result.status().message();
  EXPECT_EQ(stat_result.value().size, data.size());
  EXPECT_FALSE(stat_result.value().is_directory);

  handle->Close();
  handle.reset();

  // Re-open for read
  open_result = sdk->Open(path, OpenMode::kReadOnly);
  ASSERT_TRUE(open_result.ok()) << open_result.status().message();
  handle = std::move(open_result.value());

  // Read
  char buf[64] = {};
  auto read_result = handle->Read(buf, 0, sizeof(buf));
  ASSERT_TRUE(read_result.ok()) << read_result.status().message();
  EXPECT_EQ(read_result.value(), data.size());
  EXPECT_EQ(std::string(buf, read_result.value()), data);

  handle->Close();
}

TEST_F(SdkTest, StatReturnsCorrectMetadata) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("sdk-stat", std::move(fake));

  SetupMountAndWorker("sdk-stat");

  auto sdk_result = FluxCacheSDK::Create(MakeSdkConfig());
  ASSERT_TRUE(sdk_result.ok()) << sdk_result.status().message();
  auto sdk = std::move(sdk_result.value());

  const std::string path = "/mnt/stat_test.dat";
  ASSERT_TRUE(sdk->Create(path).ok());

  auto stat_result = sdk->Stat(path);
  ASSERT_TRUE(stat_result.ok()) << stat_result.status().message();
  const auto &fi = stat_result.value();
  EXPECT_GT(fi.inode_id, 0u);
  EXPECT_EQ(fi.size, 0u);
  EXPECT_FALSE(fi.is_directory);
}

TEST_F(SdkTest, OpenNonexistentReturnsNotFound) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("other.dat", 10, 0);
  RegisterFakeUfsForTest("sdk-notfound", std::move(fake));

  SetupMountAndWorker("sdk-notfound");

  auto sdk_result = FluxCacheSDK::Create(MakeSdkConfig());
  ASSERT_TRUE(sdk_result.ok()) << sdk_result.status().message();
  auto sdk = std::move(sdk_result.value());

  auto open_result = sdk->Open("/mnt/nonexistent.dat", OpenMode::kReadOnly);
  EXPECT_FALSE(open_result.ok());
  EXPECT_EQ(open_result.status().code(), StatusCode::kNotFound);
}

TEST_F(SdkTest, CreateAlreadyExistsReturnsAlreadyExists) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("sdk-exists", std::move(fake));

  SetupMountAndWorker("sdk-exists");

  auto sdk_result = FluxCacheSDK::Create(MakeSdkConfig());
  ASSERT_TRUE(sdk_result.ok()) << sdk_result.status().message();
  auto sdk = std::move(sdk_result.value());

  const std::string path = "/mnt/exists_test.dat";
  ASSERT_TRUE(sdk->Create(path).ok());

  auto create_status = sdk->Create(path);
  EXPECT_FALSE(create_status.ok());
  EXPECT_EQ(create_status.code(), StatusCode::kAlreadyExists);
}

TEST_F(SdkTest, DeleteSucceedsAndFileIsGone) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("sdk-delete", std::move(fake));

  SetupMountAndWorker("sdk-delete");

  auto sdk_result = FluxCacheSDK::Create(MakeSdkConfig());
  ASSERT_TRUE(sdk_result.ok()) << sdk_result.status().message();
  auto sdk = std::move(sdk_result.value());

  const std::string path = "/mnt/delete_test.dat";
  ASSERT_TRUE(sdk->Create(path).ok());

  auto delete_status = sdk->Delete(path);
  EXPECT_TRUE(delete_status.ok()) << delete_status.message();

  // File should be gone: Open returns NotFound
  auto open_result = sdk->Open(path, OpenMode::kReadOnly);
  EXPECT_FALSE(open_result.ok());
  EXPECT_EQ(open_result.status().code(), StatusCode::kNotFound);
}

TEST_F(SdkTest, InvalidConfigReturnsError) {
  SDKConfig config;
  config.master_host = "";
  config.master_port = 9090;
  auto result = FluxCacheSDK::Create(config);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);

  config.master_host = "127.0.0.1";
  config.master_port = 0;
  result = FluxCacheSDK::Create(config);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);
}

TEST_F(SdkTest, LocalCacheFirstReadThenSecondReadHitsL1) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("sdk-l1", std::move(fake));

  SetupMountAndWorker("sdk-l1");

  SDKConfig cfg = MakeSdkConfig();
  cfg.local_cache_enabled = true;
  cfg.local_cache_size_mb = 16;
  auto sdk_result = FluxCacheSDK::Create(cfg);
  ASSERT_TRUE(sdk_result.ok()) << sdk_result.status().message();
  auto sdk = std::move(sdk_result.value());

  const std::string path = "/mnt/l1_test.dat";
  const std::string data = "L1 cache test data";

  ASSERT_TRUE(sdk->Create(path).ok());
  auto open_result = sdk->Open(path, OpenMode::kReadWrite);
  ASSERT_TRUE(open_result.ok()) << open_result.status().message();
  auto handle = std::move(open_result.value());
  ASSERT_TRUE(handle->Write(0, data).ok());
  handle->Close();
  handle.reset();

  open_result = sdk->Open(path, OpenMode::kReadOnly);
  ASSERT_TRUE(open_result.ok()) << open_result.status().message();
  handle = std::move(open_result.value());

  char buf1[64] = {};
  auto r1 = handle->Read(buf1, 0, sizeof(buf1));
  ASSERT_TRUE(r1.ok()) << r1.status().message();
  EXPECT_EQ(std::string(buf1, r1.value()), data);

  char buf2[64] = {};
  auto r2 = handle->Read(buf2, 0, sizeof(buf2));
  ASSERT_TRUE(r2.ok()) << r2.status().message();
  EXPECT_EQ(std::string(buf2, r2.value()), data);

  handle->Close();
}

TEST_F(SdkTest, LocalCacheDisabledReadsWork) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("sdk-nocache", std::move(fake));

  SetupMountAndWorker("sdk-nocache");

  SDKConfig cfg = MakeSdkConfig();
  cfg.local_cache_enabled = false;
  auto sdk_result = FluxCacheSDK::Create(cfg);
  ASSERT_TRUE(sdk_result.ok()) << sdk_result.status().message();
  auto sdk = std::move(sdk_result.value());

  const std::string path = "/mnt/nocache_test.dat";
  const std::string data = "No cache";

  ASSERT_TRUE(sdk->Create(path).ok());
  auto open_result = sdk->Open(path, OpenMode::kReadWrite);
  ASSERT_TRUE(open_result.ok()) << open_result.status().message();
  auto handle = std::move(open_result.value());
  ASSERT_TRUE(handle->Write(0, data).ok());
  handle->Close();
  handle.reset();

  open_result = sdk->Open(path, OpenMode::kReadOnly);
  ASSERT_TRUE(open_result.ok()) << open_result.status().message();
  handle = std::move(open_result.value());

  char buf[64] = {};
  auto r = handle->Read(buf, 0, sizeof(buf));
  ASSERT_TRUE(r.ok()) << r.status().message();
  EXPECT_EQ(std::string(buf, r.value()), data);

  handle->Close();
}

// ---- Namespace operation tests (P5-02) ----

TEST_F(SdkTest, MkdirCreatesDirectory) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("sdk-mkdir", std::move(fake));

  SetupMountAndWorker("sdk-mkdir");

  auto sdk_result = FluxCacheSDK::Create(MakeSdkConfig());
  ASSERT_TRUE(sdk_result.ok()) << sdk_result.status().message();
  auto sdk = std::move(sdk_result.value());

  // Create directory
  auto mkdir_status = sdk->Mkdir("/mnt/mydir");
  ASSERT_TRUE(mkdir_status.ok()) << mkdir_status.message();

  // Stat should show it as directory
  auto stat_result = sdk->Stat("/mnt/mydir");
  ASSERT_TRUE(stat_result.ok()) << stat_result.status().message();
  EXPECT_TRUE(stat_result.value().is_directory);
}

TEST_F(SdkTest, MkdirAlreadyExistsReturnsAlreadyExists) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("sdk-mkdir2", std::move(fake));

  SetupMountAndWorker("sdk-mkdir2");

  auto sdk_result = FluxCacheSDK::Create(MakeSdkConfig());
  ASSERT_TRUE(sdk_result.ok()) << sdk_result.status().message();
  auto sdk = std::move(sdk_result.value());

  ASSERT_TRUE(sdk->Mkdir("/mnt/mydir").ok());

  auto mkdir_status = sdk->Mkdir("/mnt/mydir");
  EXPECT_FALSE(mkdir_status.ok());
  EXPECT_EQ(mkdir_status.code(), StatusCode::kAlreadyExists);
}

TEST_F(SdkTest, RmdirRemovesEmptyDirectory) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("sdk-rmdir", std::move(fake));

  SetupMountAndWorker("sdk-rmdir");

  auto sdk_result = FluxCacheSDK::Create(MakeSdkConfig());
  ASSERT_TRUE(sdk_result.ok()) << sdk_result.status().message();
  auto sdk = std::move(sdk_result.value());

  ASSERT_TRUE(sdk->Mkdir("/mnt/emptydir").ok());

  // Remove empty directory
  auto rmdir_status = sdk->Rmdir("/mnt/emptydir");
  EXPECT_TRUE(rmdir_status.ok()) << rmdir_status.message();

  // Directory should be gone
  auto stat_result = sdk->Stat("/mnt/emptydir");
  EXPECT_FALSE(stat_result.ok());
  EXPECT_EQ(stat_result.status().code(), StatusCode::kNotFound);
}

TEST_F(SdkTest, RmdirNonEmptyDirectoryReturnsDirectoryNotEmpty) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("sdk-rmdir-nonempty", std::move(fake));

  SetupMountAndWorker("sdk-rmdir-nonempty");

  auto sdk_result = FluxCacheSDK::Create(MakeSdkConfig());
  ASSERT_TRUE(sdk_result.ok()) << sdk_result.status().message();
  auto sdk = std::move(sdk_result.value());

  ASSERT_TRUE(sdk->Mkdir("/mnt/nonemptydir").ok());
  ASSERT_TRUE(sdk->Create("/mnt/nonemptydir/file.txt").ok());

  auto rmdir_status = sdk->Rmdir("/mnt/nonemptydir");
  EXPECT_FALSE(rmdir_status.ok());
  EXPECT_EQ(rmdir_status.code(), StatusCode::kDirectoryNotEmpty);
  EXPECT_NE(rmdir_status.message().find("directory not empty"),
            std::string::npos);
}

TEST_F(SdkTest, ListDirectoryReturnsEntries) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("sdk-listdir", std::move(fake));

  SetupMountAndWorker("sdk-listdir");

  auto sdk_result = FluxCacheSDK::Create(MakeSdkConfig());
  ASSERT_TRUE(sdk_result.ok()) << sdk_result.status().message();
  auto sdk = std::move(sdk_result.value());

  // Create directory and files
  ASSERT_TRUE(sdk->Mkdir("/mnt/dir1").ok());
  ASSERT_TRUE(sdk->Create("/mnt/dir1/file1.txt").ok());
  ASSERT_TRUE(sdk->Create("/mnt/dir1/file2.txt").ok());

  // List directory
  auto list_result = sdk->ListDirectory("/mnt/dir1");
  ASSERT_TRUE(list_result.ok()) << list_result.status().message();
  const auto &entries = list_result.value();
  EXPECT_EQ(entries.size(), 2u);

  // Check entry names
  std::vector<std::string> names;
  for (const auto &e : entries) {
    names.push_back(e.name);
    EXPECT_GT(e.info.inode_id, 0u);
    EXPECT_FALSE(e.info.is_directory);
  }
  EXPECT_NE(std::find(names.begin(), names.end(), "file1.txt"), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "file2.txt"), names.end());
}

TEST_F(SdkTest, RenameMovesFile) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("sdk-rename", std::move(fake));

  SetupMountAndWorker("sdk-rename");

  auto sdk_result = FluxCacheSDK::Create(MakeSdkConfig());
  ASSERT_TRUE(sdk_result.ok()) << sdk_result.status().message();
  auto sdk = std::move(sdk_result.value());

  // Create and write file
  ASSERT_TRUE(sdk->Create("/mnt/original.txt").ok());
  auto open_result = sdk->Open("/mnt/original.txt", OpenMode::kWriteOnly);
  ASSERT_TRUE(open_result.ok()) << open_result.status().message();
  auto handle = std::move(open_result.value());
  ASSERT_TRUE(handle->Write(0, "rename test").ok());
  handle->Close();

  // Rename
  auto rename_status = sdk->Rename("/mnt/original.txt", "/mnt/renamed.txt");
  EXPECT_TRUE(rename_status.ok()) << rename_status.message();

  // Old path should be gone
  EXPECT_FALSE(sdk->Exists("/mnt/original.txt"));

  // New path should exist
  EXPECT_TRUE(sdk->Exists("/mnt/renamed.txt"));
}

TEST_F(SdkTest, ExistsReturnsCorrectly) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("placeholder", 0, 0);
  RegisterFakeUfsForTest("sdk-exists", std::move(fake));

  SetupMountAndWorker("sdk-exists");

  auto sdk_result = FluxCacheSDK::Create(MakeSdkConfig());
  ASSERT_TRUE(sdk_result.ok()) << sdk_result.status().message();
  auto sdk = std::move(sdk_result.value());

  // Non-existent path
  EXPECT_FALSE(sdk->Exists("/mnt/no_such_file"));

  // Create file, then exists
  ASSERT_TRUE(sdk->Create("/mnt/exists_test.dat").ok());
  EXPECT_TRUE(sdk->Exists("/mnt/exists_test.dat"));

  // Directory exists
  ASSERT_TRUE(sdk->Mkdir("/mnt/exists_dir").ok());
  EXPECT_TRUE(sdk->Exists("/mnt/exists_dir"));
}

}  // namespace fluxcache
