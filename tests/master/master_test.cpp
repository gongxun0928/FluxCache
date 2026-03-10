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
  EXPECT_EQ(ring_resp.ring_version(), 0u);
  EXPECT_EQ(ring_resp.workers_size(), 1);
  EXPECT_EQ(ring_resp.workers(0).worker_id(), reg_resp.worker_id());
  EXPECT_EQ(ring_resp.workers(0).host(), "127.0.0.1");
  EXPECT_EQ(ring_resp.workers(0).port(), 9091u);
}

}  // namespace fluxcache
