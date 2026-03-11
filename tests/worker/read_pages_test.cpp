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

class ReadPagesTest : public ::testing::Test {
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

TEST_F(ReadPagesTest, FirstReadMissesThenFillsCache) {
  const std::string kContent(kPageSize, 'A');
  constexpr int64_t kMtime = 1234567890;
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("file.dat", kContent, kMtime);
  RegisterFakeUfsForTest("rp-miss", std::move(fake));

  std::unique_ptr<UFS> ufs;
  ASSERT_TRUE(CreateUFS("fake", "rp-miss", &ufs).ok());
  FakeUfs* fake_ptr = dynamic_cast<FakeUfs*>(ufs.get());
  ASSERT_NE(fake_ptr, nullptr);
  EXPECT_EQ(fake_ptr->read_count(), 0);

  BlockId block_id = MakeBlockId(1, 0);
  ::grpc::ServerContext ctx;
  proto::ReadPagesRequest req;
  req.set_block_id(block_id);
  req.add_page_indices(0);
  req.set_expected_mtime_ms(kMtime);
  req.set_ufs_uri("fake://rp-miss");
  req.set_ufs_path("file.dat");

  proto::ReadPagesResponse resp;
  auto status = service_impl_->ReadPages(&ctx, &req, &resp);

  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(resp.data().size(), kPageSize);
  EXPECT_EQ(resp.data(), kContent);
  EXPECT_EQ(fake_ptr->read_count(), 1) << "First read should fetch from UFS";

  req.Clear();
  resp.Clear();
  req.set_block_id(block_id);
  req.add_page_indices(0);
  req.set_expected_mtime_ms(kMtime);
  req.set_ufs_uri("fake://rp-miss");
  req.set_ufs_path("file.dat");
  status = service_impl_->ReadPages(&ctx, &req, &resp);

  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(resp.data(), kContent);
  EXPECT_EQ(fake_ptr->read_count(), 1) << "Second read should hit cache";
}

TEST_F(ReadPagesTest, MtimeMismatchEvictsAndRefetches) {
  const std::string kContent(kPageSize, 'B');
  constexpr int64_t kMtime1 = 1000;
  constexpr int64_t kMtime2 = 2000;
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("file.dat", kContent, kMtime2);
  RegisterFakeUfsForTest("rp-mtime", std::move(fake));

  std::unique_ptr<UFS> ufs;
  ASSERT_TRUE(CreateUFS("fake", "rp-mtime", &ufs).ok());
  FakeUfs* fake_ptr = dynamic_cast<FakeUfs*>(ufs.get());
  ASSERT_NE(fake_ptr, nullptr);

  BlockId block_id = MakeBlockId(2, 0);
  ::grpc::ServerContext ctx;
  proto::ReadPagesRequest req;
  req.set_block_id(block_id);
  req.add_page_indices(0);
  req.set_expected_mtime_ms(kMtime1);
  req.set_ufs_uri("fake://rp-mtime");
  req.set_ufs_path("file.dat");

  proto::ReadPagesResponse resp;
  auto status = service_impl_->ReadPages(&ctx, &req, &resp);
  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(fake_ptr->read_count(), 1);

  req.set_expected_mtime_ms(kMtime2);
  status = service_impl_->ReadPages(&ctx, &req, &resp);
  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(fake_ptr->read_count(), 2)
      << "Mtime mismatch should evict and refetch from UFS";
}

TEST_F(ReadPagesTest, MultiPageReturnsInOrder) {
  std::string p0(kPageSize, '0');
  std::string p1(kPageSize, '1');
  std::string p2(kPageSize, '2');
  std::string all = p0 + p1 + p2;
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("multi.dat", all, 9999);
  RegisterFakeUfsForTest("rp-multi", std::move(fake));

  BlockId block_id = MakeBlockId(3, 0);
  ::grpc::ServerContext ctx;
  proto::ReadPagesRequest req;
  req.set_block_id(block_id);
  req.add_page_indices(0);
  req.add_page_indices(1);
  req.add_page_indices(2);
  req.set_expected_mtime_ms(9999);
  req.set_ufs_uri("fake://rp-multi");
  req.set_ufs_path("multi.dat");

  proto::ReadPagesResponse resp;
  auto status = service_impl_->ReadPages(&ctx, &req, &resp);

  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(resp.data().size(), 3 * kPageSize);
  EXPECT_EQ(resp.data().substr(0, kPageSize), p0);
  EXPECT_EQ(resp.data().substr(kPageSize, kPageSize), p1);
  EXPECT_EQ(resp.data().substr(2 * kPageSize, kPageSize), p2);
}

TEST_F(ReadPagesTest, InvalidUfsUriReturnsError) {
  BlockId block_id = MakeBlockId(1, 0);
  ::grpc::ServerContext ctx;
  proto::ReadPagesRequest req;
  req.set_block_id(block_id);
  req.add_page_indices(0);
  req.set_expected_mtime_ms(0);
  req.set_ufs_uri("invalid-no-scheme");
  req.set_ufs_path("file.dat");

  proto::ReadPagesResponse resp;
  auto status = service_impl_->ReadPages(&ctx, &req, &resp);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_F(ReadPagesTest, MissingUfsUriReturnsError) {
  BlockId block_id = MakeBlockId(1, 0);
  ::grpc::ServerContext ctx;
  proto::ReadPagesRequest req;
  req.set_block_id(block_id);
  req.add_page_indices(0);
  req.set_expected_mtime_ms(0);
  req.set_ufs_path("file.dat");

  proto::ReadPagesResponse resp;
  auto status = service_impl_->ReadPages(&ctx, &req, &resp);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
}

}  // namespace fluxcache
