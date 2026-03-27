#include "master/inode_tree.h"
#include "master/master_service_impl.h"
#include "master.pb.h"
#include "ufs/fake_ufs.h"
#include "ufs/ufs_factory.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <grpcpp/grpcpp.h>
#include <thread>

namespace fluxcache {

class PrewarmTest : public ::testing::Test {
 protected:
  void SetUp() override {
    namespace fs = std::filesystem;
    auto base = fs::temp_directory_path() / "fluxcache_prewarm_test";
    fs::create_directories(base);
    db_path_ = (base / "db").string();

    tree_ = std::make_unique<InodeTree>(db_path_);
    ASSERT_TRUE(tree_->InitOrRecover()) << "InodeTree init failed";
  }

  void TearDown() override {
    tree_.reset();
    std::filesystem::remove_all(
        std::filesystem::temp_directory_path() / "fluxcache_prewarm_test");
  }

  std::string db_path_;
  std::unique_ptr<InodeTree> tree_;
};

TEST_F(PrewarmTest, SubmitPrewarmEnqueuesAndProcesses) {
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFile("a.txt", 100, 0);
  fake->AddFile("b.txt", 200, 0);
  RegisterFakeUfsForTest("prewarm-1", std::move(fake));

  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;

  proto::MountRequest mount_req;
  mount_req.set_path("/mnt");
  mount_req.set_ufs_uri("fake://prewarm-1");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl.Mount(&ctx, &mount_req, &mount_resp).ok());

  proto::SubmitPrewarmRequest prewarm_req;
  prewarm_req.set_path("/mnt");
  proto::SubmitPrewarmResponse prewarm_resp;
  auto status = impl.SubmitPrewarm(&ctx, &prewarm_req, &prewarm_resp);
  ASSERT_TRUE(status.ok()) << status.error_message();

  // Wait for background prewarm to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Verify InodeTree has entries
  EXPECT_TRUE(tree_->LookupPath("/mnt/a.txt").has_value());
  EXPECT_TRUE(tree_->LookupPath("/mnt/b.txt").has_value());
}

TEST_F(PrewarmTest, SubmitPrewarmEmptyPathReturnsInvalidArgument) {
  MasterServiceImpl impl(tree_.get());
  ::grpc::ServerContext ctx;

  proto::SubmitPrewarmRequest req;
  proto::SubmitPrewarmResponse resp;
  auto status = impl.SubmitPrewarm(&ctx, &req, &resp);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
}

}  // namespace fluxcache
