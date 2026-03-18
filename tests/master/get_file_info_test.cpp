#include "master/inode_tree.h"
#include "master/master_service_impl.h"
#include "master.pb.h"
#include "ufs/fake_ufs.h"
#include "ufs/ufs_factory.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <grpcpp/grpcpp.h>

namespace fluxcache {

class GetFileInfoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    namespace fs = std::filesystem;
    auto base = fs::temp_directory_path() / "fluxcache_getfileinfo_test";
    fs::create_directories(base);
    db_path_ = (base / "db").string();

    tree_ = std::make_unique<InodeTree>(db_path_);
    ASSERT_TRUE(tree_->InitOrRecover()) << "InodeTree init failed";
  }

  void TearDown() override {
    tree_.reset();
    std::filesystem::remove_all(
        std::filesystem::temp_directory_path() / "fluxcache_getfileinfo_test");
  }

  std::string db_path_;
  std::unique_ptr<InodeTree> tree_;
};

TEST_F(GetFileInfoTest, ExistingFileReturnsFullFileInfo) {
  auto fake = std::make_unique<FakeUfs>();
  constexpr int64_t kMtimeMs = 1234567890123;
  constexpr uint64_t kSize = 4096;
  fake->AddFile("file.txt", kSize, kMtimeMs);
  RegisterFakeUfsForTest("gfi-existing", std::move(fake));

  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;

  proto::MountRequest mount_req;
  mount_req.set_path("/mnt");
  mount_req.set_ufs_uri("fake://gfi-existing");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl.Mount(&ctx, &mount_req, &mount_resp).ok());

  proto::GetFileInfoRequest req;
  req.set_path("/mnt/file.txt");
  proto::GetFileInfoResponse resp;
  auto status = impl.GetFileInfo(&ctx, &req, &resp);

  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_GT(resp.file_info().inode_id(), 0u);
  EXPECT_EQ(resp.file_info().size(), kSize);
  EXPECT_EQ(resp.file_info().file_version(), 0u)
      << "synced file has file_version 0";
  EXPECT_GT(resp.file_info().block_size(), 0u);
  EXPECT_FALSE(resp.file_info().is_directory());
  EXPECT_EQ(resp.ufs_uri(), "fake://gfi-existing");
  EXPECT_EQ(resp.ufs_path(), "file.txt");
}

TEST_F(GetFileInfoTest, NonexistentPathReturnsNotFound) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("only.txt", 100, 0);
  RegisterFakeUfsForTest("gfi-nonexist", std::move(fake));

  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;

  proto::MountRequest mount_req;
  mount_req.set_path("/mnt");
  mount_req.set_ufs_uri("fake://gfi-nonexist");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl.Mount(&ctx, &mount_req, &mount_resp).ok());

  proto::GetFileInfoRequest req;
  req.set_path("/mnt/nonexistent.txt");
  proto::GetFileInfoResponse resp;
  auto status = impl.GetFileInfo(&ctx, &req, &resp);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::NOT_FOUND);
}

TEST_F(GetFileInfoTest, WorkersAndRingVersionMatchGetHashRing) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("f.txt", 1, 0);
  RegisterFakeUfsForTest("gfi-ring", std::move(fake));

  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;

  proto::RegisterWorkerRequest reg_req;
  reg_req.mutable_endpoint()->set_host("127.0.0.1");
  reg_req.mutable_endpoint()->set_port(19999);
  proto::RegisterWorkerResponse reg_resp;
  ASSERT_TRUE(impl.RegisterWorker(&ctx, &reg_req, &reg_resp).ok());
  uint64_t worker_id = reg_resp.worker_id();

  proto::MountRequest mount_req;
  mount_req.set_path("/mnt");
  mount_req.set_ufs_uri("fake://gfi-ring");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl.Mount(&ctx, &mount_req, &mount_resp).ok());

  proto::GetHashRingRequest ring_req;
  proto::GetHashRingResponse ring_resp;
  ASSERT_TRUE(impl.GetHashRing(&ctx, &ring_req, &ring_resp).ok());

  proto::GetFileInfoRequest fi_req;
  fi_req.set_path("/mnt/f.txt");
  proto::GetFileInfoResponse fi_resp;
  ASSERT_TRUE(impl.GetFileInfo(&ctx, &fi_req, &fi_resp).ok());

  EXPECT_EQ(fi_resp.ring_version(), ring_resp.ring_version());
  ASSERT_EQ(fi_resp.workers_size(), ring_resp.workers_size());
  EXPECT_EQ(fi_resp.workers(0).worker_id(), worker_id);
  EXPECT_EQ(fi_resp.workers(0).worker_id(), ring_resp.workers(0).worker_id());
}

TEST_F(GetFileInfoTest, SyncedFileHasFileVersionZero) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("mtime_test.dat", 1024, 0);
  RegisterFakeUfsForTest("gfi-mtime", std::move(fake));

  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;

  proto::MountRequest mount_req;
  mount_req.set_path("/data");
  mount_req.set_ufs_uri("fake://gfi-mtime");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl.Mount(&ctx, &mount_req, &mount_resp).ok());

  proto::GetFileInfoRequest req;
  req.set_path("/data/mtime_test.dat");
  proto::GetFileInfoResponse resp;
  auto status = impl.GetFileInfo(&ctx, &req, &resp);

  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(resp.file_info().file_version(), 0u)
      << "synced file has file_version 0";
}

TEST_F(GetFileInfoTest, EmptyPathReturnsInvalidArgument) {
  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;

  proto::GetFileInfoRequest req;
  req.set_path("");
  proto::GetFileInfoResponse resp;
  auto status = impl.GetFileInfo(&ctx, &req, &resp);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
}

}  // namespace fluxcache
