// Contract tests for Phase 1 proto: encode/decode and field accessibility.

#include <gtest/gtest.h>
#include <string>

#include "common.pb.h"
#include "master.pb.h"
#include "worker.pb.h"

namespace fluxcache {
namespace proto {
namespace {

TEST(ProtoContractTest, FileInfoEncodeDecode) {
  FileInfo orig;
  orig.set_inode_id(42);
  orig.set_size(1024);
  orig.set_block_size(64 * 1024 * 1024);
  orig.set_file_version(1234567890);
  orig.set_is_directory(false);

  std::string serialized;
  ASSERT_TRUE(orig.SerializeToString(&serialized));

  FileInfo parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));

  EXPECT_EQ(parsed.inode_id(), 42u);
  EXPECT_EQ(parsed.size(), 1024u);
  EXPECT_EQ(parsed.block_size(), 64u * 1024 * 1024);
  EXPECT_EQ(parsed.file_version(), 1234567890);
  EXPECT_FALSE(parsed.is_directory());
}

TEST(ProtoContractTest, GetFileInfoResponseFields) {
  GetFileInfoResponse resp;
  resp.mutable_file_info()->set_inode_id(1);
  resp.mutable_file_info()->set_size(100);
  resp.mutable_file_info()->set_block_size(65536);
  resp.mutable_file_info()->set_file_version(999);
  resp.mutable_file_info()->set_is_directory(false);
  resp.set_ring_version(5);
  auto* w = resp.add_workers();
  w->set_worker_id(10);
  w->set_host("127.0.0.1");
  w->set_port(9000);
  resp.set_ufs_uri("file:///data");
  resp.set_ufs_path("/mnt/ufs/file.txt");

  std::string serialized;
  ASSERT_TRUE(resp.SerializeToString(&serialized));

  GetFileInfoResponse parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));

  EXPECT_EQ(parsed.file_info().inode_id(), 1u);
  EXPECT_EQ(parsed.file_info().size(), 100u);
  EXPECT_EQ(parsed.file_info().block_size(), 65536u);
  EXPECT_EQ(parsed.file_info().file_version(), 999);
  EXPECT_FALSE(parsed.file_info().is_directory());
  EXPECT_EQ(parsed.ring_version(), 5u);
  ASSERT_EQ(parsed.workers_size(), 1);
  EXPECT_EQ(parsed.workers(0).worker_id(), 10u);
  EXPECT_EQ(parsed.workers(0).host(), "127.0.0.1");
  EXPECT_EQ(parsed.workers(0).port(), 9000u);
  EXPECT_EQ(parsed.ufs_uri(), "file:///data");
  EXPECT_EQ(parsed.ufs_path(), "/mnt/ufs/file.txt");
}

TEST(ProtoContractTest, ReadPagesRequestFields) {
  ReadPagesRequest req;
  req.set_block_id(0x123456789ABCDEF0);
  req.add_page_indices(0);
  req.add_page_indices(1);
  req.set_expected_file_version(12345);
  req.set_ufs_uri("file:///data");
  req.set_ufs_path("/path/to/file");

  std::string serialized;
  ASSERT_TRUE(req.SerializeToString(&serialized));

  ReadPagesRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));

  EXPECT_EQ(parsed.block_id(), 0x123456789ABCDEF0u);
  ASSERT_EQ(parsed.page_indices_size(), 2);
  EXPECT_EQ(parsed.page_indices(0), 0u);
  EXPECT_EQ(parsed.page_indices(1), 1u);
  EXPECT_EQ(parsed.expected_file_version(), 12345);
  EXPECT_EQ(parsed.ufs_uri(), "file:///data");
  EXPECT_EQ(parsed.ufs_path(), "/path/to/file");
}

TEST(ProtoContractTest, WritePagesRequestFields) {
  WritePagesRequest req;
  req.set_block_id(100);
  req.add_page_indices(0);
  req.set_data("hello world");
  req.set_ufs_uri("file:///data");
  req.set_ufs_path("/path/to/file");

  std::string serialized;
  ASSERT_TRUE(req.SerializeToString(&serialized));

  WritePagesRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));

  EXPECT_EQ(parsed.block_id(), 100u);
  ASSERT_EQ(parsed.page_indices_size(), 1);
  EXPECT_EQ(parsed.page_indices(0), 0u);
  EXPECT_EQ(parsed.data(), "hello world");
  EXPECT_EQ(parsed.ufs_uri(), "file:///data");
  EXPECT_EQ(parsed.ufs_path(), "/path/to/file");
}

TEST(ProtoContractTest, WorkerEndpointFields) {
  WorkerEndpoint ep;
  ep.set_worker_id(1);
  ep.set_host("localhost");
  ep.set_port(8080);

  std::string serialized;
  ASSERT_TRUE(ep.SerializeToString(&serialized));

  WorkerEndpoint parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));

  EXPECT_EQ(parsed.worker_id(), 1u);
  EXPECT_EQ(parsed.host(), "localhost");
  EXPECT_EQ(parsed.port(), 8080u);
}

TEST(ProtoContractTest, HeartbeatReservedFields) {
  HeartbeatRequest req;
  req.set_worker_id(1);
  req.add_orphan_inode_ids(100);
  req.add_misplaced_block_ids(200);

  std::string serialized;
  ASSERT_TRUE(req.SerializeToString(&serialized));

  HeartbeatRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));

  EXPECT_EQ(parsed.worker_id(), 1u);
  ASSERT_EQ(parsed.orphan_inode_ids_size(), 1);
  EXPECT_EQ(parsed.orphan_inode_ids(0), 100u);
  ASSERT_EQ(parsed.misplaced_block_ids_size(), 1);
  EXPECT_EQ(parsed.misplaced_block_ids(0), 200u);
}

}  // namespace
}  // namespace proto
}  // namespace fluxcache
