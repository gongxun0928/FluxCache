#include "master/master_service_impl.h"
#include "master.pb.h"
#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

namespace fluxcache {

TEST(MasterServiceTest, GetHashRingReturnsMinimalResponse) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;
  proto::GetHashRingRequest req;
  proto::GetHashRingResponse resp;

  auto status = impl.GetHashRing(&ctx, &req, &resp);

  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(resp.ring_version(), 0u);
  EXPECT_EQ(resp.workers_size(), 0);
}

TEST(MasterServiceTest, RegisterWorkerValidReturnsSuccess) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;
  proto::RegisterWorkerRequest req;
  req.mutable_endpoint()->set_host("127.0.0.1");
  req.mutable_endpoint()->set_port(9091);
  proto::RegisterWorkerResponse resp;

  auto status = impl.RegisterWorker(&ctx, &req, &resp);

  ASSERT_TRUE(status.ok()) << status.error_message();
  EXPECT_GT(resp.worker_id(), 0u);
}

TEST(MasterServiceTest, RegisterWorkerInvalidMissingEndpoint) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;
  proto::RegisterWorkerRequest req;
  proto::RegisterWorkerResponse resp;

  auto status = impl.RegisterWorker(&ctx, &req, &resp);

  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_NE(std::string(status.error_message()).find("endpoint"), std::string::npos);
}

TEST(MasterServiceTest, RegisterWorkerInvalidEmptyHost) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;
  proto::RegisterWorkerRequest req;
  req.mutable_endpoint()->set_host("");
  req.mutable_endpoint()->set_port(9091);
  proto::RegisterWorkerResponse resp;

  auto status = impl.RegisterWorker(&ctx, &req, &resp);

  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_NE(std::string(status.error_message()).find("host"), std::string::npos);
}

TEST(MasterServiceTest, RegisterWorkerInvalidZeroPort) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;
  proto::RegisterWorkerRequest req;
  req.mutable_endpoint()->set_host("127.0.0.1");
  req.mutable_endpoint()->set_port(0);
  proto::RegisterWorkerResponse resp;

  auto status = impl.RegisterWorker(&ctx, &req, &resp);

  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_NE(std::string(status.error_message()).find("port"), std::string::npos);
}

TEST(MasterServiceTest, GetHashRingReturnsRegisteredWorkers) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;

  proto::RegisterWorkerRequest reg_req;
  reg_req.mutable_endpoint()->set_host("127.0.0.1");
  reg_req.mutable_endpoint()->set_port(9091);
  proto::RegisterWorkerResponse reg_resp;
  auto reg_status = impl.RegisterWorker(&ctx, &reg_req, &reg_resp);
  ASSERT_TRUE(reg_status.ok());

  proto::GetHashRingRequest ring_req;
  proto::GetHashRingResponse ring_resp;
  auto ring_status = impl.GetHashRing(&ctx, &ring_req, &ring_resp);

  ASSERT_TRUE(ring_status.ok());
  EXPECT_GE(ring_resp.ring_version(), 1u);
  EXPECT_EQ(ring_resp.workers_size(), 1);
  EXPECT_EQ(ring_resp.workers(0).worker_id(), reg_resp.worker_id());
  EXPECT_EQ(ring_resp.workers(0).host(), "127.0.0.1");
  EXPECT_EQ(ring_resp.workers(0).port(), 9091u);
}

TEST(MasterServiceTest, MountRPC) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;
  proto::MountRequest req;
  req.set_path("/data");
  req.set_ufs_uri("local:///tmp/data");
  proto::MountResponse resp;

  auto status = impl.Mount(&ctx, &req, &resp);

  ASSERT_TRUE(status.ok()) << status.error_message();
}

TEST(MasterServiceTest, MountDuplicateReturnsError) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;
  proto::MountRequest req;
  req.set_path("/data");
  req.set_ufs_uri("local:///tmp/data");
  proto::MountResponse resp;

  auto s1 = impl.Mount(&ctx, &req, &resp);
  ASSERT_TRUE(s1.ok());

  req.set_ufs_uri("local:///other");
  auto s2 = impl.Mount(&ctx, &req, &resp);
  ASSERT_FALSE(s2.ok());
  EXPECT_EQ(s2.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_NE(std::string(s2.error_message()).find("already mounted"),
            std::string::npos);
}

TEST(MasterServiceTest, ListMountsRPC) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;
  proto::MountRequest mount_req;
  mount_req.set_path("/data");
  mount_req.set_ufs_uri("local:///tmp/data");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl.Mount(&ctx, &mount_req, &mount_resp).ok());

  proto::ListMountsRequest list_req;
  proto::ListMountsResponse list_resp;
  auto status = impl.ListMounts(&ctx, &list_req, &list_resp);

  ASSERT_TRUE(status.ok()) << status.error_message();
  ASSERT_EQ(list_resp.paths_size(), 1);
  EXPECT_EQ(list_resp.paths(0), "/data");
}

TEST(MasterServiceTest, UnmountRPC) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;
  proto::MountRequest mount_req;
  mount_req.set_path("/data");
  mount_req.set_ufs_uri("local:///tmp/data");
  proto::MountResponse mount_resp;
  ASSERT_TRUE(impl.Mount(&ctx, &mount_req, &mount_resp).ok());

  proto::UnmountRequest unmount_req;
  unmount_req.set_path("/data");
  proto::UnmountResponse unmount_resp;
  auto status = impl.Unmount(&ctx, &unmount_req, &unmount_resp);

  ASSERT_TRUE(status.ok()) << status.error_message();

  proto::ListMountsRequest list_req;
  proto::ListMountsResponse list_resp;
  ASSERT_TRUE(impl.ListMounts(&ctx, &list_req, &list_resp).ok());
  EXPECT_EQ(list_resp.paths_size(), 0);
}

TEST(MasterServiceTest, RegisterWorkerIdempotentRefresh) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;

  proto::RegisterWorkerRequest reg_req;
  reg_req.mutable_endpoint()->set_host("127.0.0.1");
  reg_req.mutable_endpoint()->set_port(9091);
  proto::RegisterWorkerResponse reg_resp;
  auto reg_status = impl.RegisterWorker(&ctx, &reg_req, &reg_resp);
  ASSERT_TRUE(reg_status.ok());
  uint64_t worker_id = reg_resp.worker_id();

  proto::RegisterWorkerRequest refresh_req;
  refresh_req.mutable_endpoint()->set_worker_id(worker_id);
  refresh_req.mutable_endpoint()->set_host("127.0.0.1");
  refresh_req.mutable_endpoint()->set_port(9091);
  proto::RegisterWorkerResponse refresh_resp;
  auto refresh_status = impl.RegisterWorker(&ctx, &refresh_req, &refresh_resp);

  ASSERT_TRUE(refresh_status.ok());
  EXPECT_EQ(refresh_resp.worker_id(), worker_id);

  proto::GetHashRingRequest ring_req;
  proto::GetHashRingResponse ring_resp;
  auto ring_status = impl.GetHashRing(&ctx, &ring_req, &ring_resp);
  ASSERT_TRUE(ring_status.ok());
  EXPECT_EQ(ring_resp.workers_size(), 1);
  EXPECT_EQ(ring_resp.workers(0).worker_id(), worker_id);
}

TEST(MasterServiceTest, RegisterWorkerIdempotentNotFound) {
  MasterServiceImpl impl;
  ::grpc::ServerContext ctx;

  proto::RegisterWorkerRequest req;
  req.mutable_endpoint()->set_worker_id(999);
  req.mutable_endpoint()->set_host("127.0.0.1");
  req.mutable_endpoint()->set_port(9091);
  proto::RegisterWorkerResponse resp;

  auto status = impl.RegisterWorker(&ctx, &req, &resp);

  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::NOT_FOUND);
}

}  // namespace fluxcache
