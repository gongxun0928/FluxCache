#include "master/inode_tree.h"
#include "master/master_service_impl.h"
#include "master.pb.h"
#include "ufs/fake_ufs.h"
#include "ufs/ufs_factory.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <grpcpp/grpcpp.h>

namespace fluxcache {

class CreateCompleteFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    namespace fs = std::filesystem;
    auto base = fs::temp_directory_path() / "fluxcache_create_complete_test";
    fs::create_directories(base);
    db_path_ = (base / "db").string();

    tree_ = std::make_unique<InodeTree>(db_path_);
    ASSERT_TRUE(tree_->InitOrRecover()) << "InodeTree init failed";
  }

  void TearDown() override {
    tree_.reset();
    std::filesystem::remove_all(
        std::filesystem::temp_directory_path() /
        "fluxcache_create_complete_test");
  }

  std::string db_path_;
  std::unique_ptr<InodeTree> tree_;
};

TEST_F(CreateCompleteFileTest, CreateFileSuccessReturnsInodeAndPathInfo) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("existing.txt", 100, 0);
  RegisterFakeUfsForTest("ccf-success", std::move(fake));

  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;

  proto::MountRequest mount_req;
  mount_req.set_path("/mnt");
  mount_req.set_ufs_uri("fake://ccf-success");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl.Mount(&ctx, &mount_req, &mount_resp).ok());

  proto::CreateFileRequest req;
  req.set_path("/mnt/newfile.dat");
  proto::CreateFileResponse resp;
  auto status = impl.CreateFile(&ctx, &req, &resp);

  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_GT(resp.file_info().inode_id(), 0u);
  EXPECT_EQ(resp.file_info().size(), 0u);
  EXPECT_FALSE(resp.file_info().is_directory());
  EXPECT_EQ(resp.ufs_uri(), "fake://ccf-success");
  EXPECT_EQ(resp.ufs_path(), "newfile.dat");
}

TEST_F(CreateCompleteFileTest, CreateFileDuplicateReturnsAlreadyExists) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("parent.txt", 1, 0);
  RegisterFakeUfsForTest("ccf-dup", std::move(fake));

  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;

  proto::MountRequest mount_req;
  mount_req.set_path("/data");
  mount_req.set_ufs_uri("fake://ccf-dup");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl.Mount(&ctx, &mount_req, &mount_resp).ok());

  proto::CreateFileRequest req;
  req.set_path("/data/newfile_only.dat");
  proto::CreateFileResponse resp;
  auto status1 = impl.CreateFile(&ctx, &req, &resp);
  ASSERT_TRUE(status1.ok()) << "First CreateFile should succeed";

  proto::CreateFileResponse resp2;
  auto status2 = impl.CreateFile(&ctx, &req, &resp2);
  EXPECT_FALSE(status2.ok());
  EXPECT_EQ(status2.error_code(), ::grpc::StatusCode::ALREADY_EXISTS);
}

TEST_F(CreateCompleteFileTest, CreateFileEmptyPathReturnsInvalidArgument) {
  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;

  proto::CreateFileRequest req;
  req.set_path("");
  proto::CreateFileResponse resp;
  auto status = impl.CreateFile(&ctx, &req, &resp);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_F(CreateCompleteFileTest, CreateFileUnmountedPathReturnsNotFound) {
  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;

  proto::CreateFileRequest req;
  req.set_path("/unmounted/path/file.txt");
  proto::CreateFileResponse resp;
  auto status = impl.CreateFile(&ctx, &req, &resp);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::NOT_FOUND);
}

TEST_F(CreateCompleteFileTest, CompleteFileUpdatesSizeAndMtime) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("dummy.txt", 1, 0);
  RegisterFakeUfsForTest("ccf-complete", std::move(fake));

  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;

  proto::MountRequest mount_req;
  mount_req.set_path("/mnt");
  mount_req.set_ufs_uri("fake://ccf-complete");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl.Mount(&ctx, &mount_req, &mount_resp).ok());

  proto::CreateFileRequest create_req;
  create_req.set_path("/mnt/created_for_complete.dat");
  proto::CreateFileResponse create_resp;
  auto create_status = impl.CreateFile(&ctx, &create_req, &create_resp);
  ASSERT_TRUE(create_status.ok()) << create_status.error_message();
  uint64_t inode_id = create_resp.file_info().inode_id();

  constexpr uint64_t kNewSize = 4096;
  proto::CompleteFileRequest complete_req;
  complete_req.set_inode_id(inode_id);
  complete_req.set_size(kNewSize);
  proto::CompleteFileResponse complete_resp;
  auto complete_status = impl.CompleteFile(&ctx, &complete_req, &complete_resp);
  ASSERT_TRUE(complete_status.ok()) << complete_status.error_message();

  proto::GetFileInfoRequest fi_req;
  fi_req.set_path("/mnt/created_for_complete.dat");
  proto::GetFileInfoResponse fi_resp;
  auto fi_status = impl.GetFileInfo(&ctx, &fi_req, &fi_resp);
  ASSERT_TRUE(fi_status.ok()) << fi_status.error_message();

  EXPECT_EQ(fi_resp.file_info().size(), kNewSize);
  EXPECT_EQ(fi_resp.file_info().file_version(), 1u)
      << "CompleteFile increments file_version from 0 to 1";
}

TEST_F(CreateCompleteFileTest, CompleteFileInvalidInodeReturnsNotFound) {
  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;

  proto::CompleteFileRequest req;
  req.set_inode_id(99999);
  req.set_size(100);
  proto::CompleteFileResponse resp;
  auto status = impl.CompleteFile(&ctx, &req, &resp);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::NOT_FOUND);
}

TEST_F(CreateCompleteFileTest, CompleteFileZeroInodeIdReturnsInvalidArgument) {
  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;

  proto::CompleteFileRequest req;
  req.set_inode_id(0);
  req.set_size(100);
  proto::CompleteFileResponse resp;
  auto status = impl.CompleteFile(&ctx, &req, &resp);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
}

}  // namespace fluxcache
