#include "common/types.h"
#include "ufs/fake_ufs.h"
#include "ufs/ufs_factory.h"
#include "worker/page/page_store.h"
#include "worker/storage/memory_tier.h"
#include "worker/worker_service_impl.h"
#include "worker.pb.h"

#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include <string>

namespace fluxcache {

class WritePagesTest : public ::testing::Test {
 protected:
  static constexpr size_t kPageSize = 4096;
  static constexpr size_t kBlockSize = 64ULL * 1024 * 1024;

  void SetUp() override {
    memory_tier_ = std::make_unique<MemoryTier>(64 * kPageSize);
    page_store_ = std::make_unique<PageStore>(memory_tier_.get(), kPageSize);
    service_impl_ = std::make_unique<WorkerServiceImpl>(
        page_store_.get(), kPageSize, kBlockSize);
  }

  std::unique_ptr<MemoryTier> memory_tier_;
  std::unique_ptr<PageStore> page_store_;
  std::unique_ptr<WorkerServiceImpl> service_impl_;
};

TEST_F(WritePagesTest, WriteThenReadReturnsNewContentAndMtime) {
  const std::string kInitialContent(kPageSize, 'A');
  constexpr int64_t kInitialMtime = 1000;
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("file.dat", kInitialContent, kInitialMtime);
  RegisterFakeUfsForTest("wp-success", std::move(fake));

  BlockId block_id = MakeBlockId(1, 0);
  const std::string kNewContent(kPageSize, 'B');

  ::grpc::ServerContext ctx;
  proto::WritePagesRequest req;
  req.set_block_id(block_id);
  req.add_page_indices(0);
  req.set_data(kNewContent);
  req.set_ufs_uri("fake://wp-success");
  req.set_ufs_path("file.dat");

  proto::WritePagesResponse resp;
  auto status = service_impl_->WritePages(&ctx, &req, &resp);

  ASSERT_TRUE(status.ok()) << status.error_message();

  // ReadPages with new mtime (FakeUfs increments mtime after Write)
  std::unique_ptr<UFS> ufs;
  ASSERT_TRUE(CreateUFS("fake", "wp-success", &ufs).ok());
  FileStatus fs;
  ASSERT_TRUE(ufs->GetStatus("file.dat", &fs).ok());
  int64_t new_mtime = fs.mtime_ms;
  ASSERT_GT(new_mtime, kInitialMtime) << "mtime should increase after Write";

  proto::ReadPagesRequest read_req;
  read_req.set_block_id(block_id);
  read_req.add_page_indices(0);
  read_req.set_expected_mtime_ms(new_mtime);
  read_req.set_ufs_uri("fake://wp-success");
  read_req.set_ufs_path("file.dat");

  proto::ReadPagesResponse read_resp;
  status = service_impl_->ReadPages(&ctx, &read_req, &read_resp);

  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(read_resp.data().size(), kPageSize);
  EXPECT_EQ(read_resp.data(), kNewContent);
}

TEST_F(WritePagesTest, UfsWriteFailureReturnsErrorNoCacheUpdate) {
  const std::string kContent(kPageSize, 'X');
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("file.dat", kContent, 5000);
  fake->SetWriteFail(true);
  RegisterFakeUfsForTest("wp-fail", std::move(fake));

  BlockId block_id = MakeBlockId(2, 0);

  ::grpc::ServerContext ctx;
  proto::WritePagesRequest req;
  req.set_block_id(block_id);
  req.add_page_indices(0);
  req.set_data(kContent);
  req.set_ufs_uri("fake://wp-fail");
  req.set_ufs_path("file.dat");

  proto::WritePagesResponse resp;
  auto status = service_impl_->WritePages(&ctx, &req, &resp);

  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INTERNAL);

  // PageStore should not have the page (no pseudo-success cache)
  PageId id{block_id, 0};
  EXPECT_FALSE(page_store_->Contains(id));
}

TEST_F(WritePagesTest, InvalidRequestMissingBlockIdReturnsError) {
  const std::string kContent(kPageSize, 'X');
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("file.dat", kContent, 1000);
  RegisterFakeUfsForTest("wp-invalid", std::move(fake));

  ::grpc::ServerContext ctx;
  proto::WritePagesRequest req;
  req.set_block_id(kInvalidBlockId);
  req.add_page_indices(0);
  req.set_data(kContent);
  req.set_ufs_uri("fake://wp-invalid");
  req.set_ufs_path("file.dat");

  proto::WritePagesResponse resp;
  auto status = service_impl_->WritePages(&ctx, &req, &resp);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_F(WritePagesTest, InvalidRequestWrongDataLengthReturnsError) {
  const std::string kContent(kPageSize, 'X');
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("file.dat", kContent, 1000);
  RegisterFakeUfsForTest("wp-badlen", std::move(fake));

  ::grpc::ServerContext ctx;
  proto::WritePagesRequest req;
  req.set_block_id(MakeBlockId(1, 0));
  req.add_page_indices(0);
  req.set_data("short");
  req.set_ufs_uri("fake://wp-badlen");
  req.set_ufs_path("file.dat");

  proto::WritePagesResponse resp;
  auto status = service_impl_->WritePages(&ctx, &req, &resp);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_F(WritePagesTest, InvalidRequestMissingUfsUriReturnsError) {
  const std::string kContent(kPageSize, 'X');
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("file.dat", kContent, 1000);
  RegisterFakeUfsForTest("wp-nouri", std::move(fake));

  ::grpc::ServerContext ctx;
  proto::WritePagesRequest req;
  req.set_block_id(MakeBlockId(1, 0));
  req.add_page_indices(0);
  req.set_data(kContent);
  req.set_ufs_path("file.dat");

  proto::WritePagesResponse resp;
  auto status = service_impl_->WritePages(&ctx, &req, &resp);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
}

}  // namespace fluxcache
