#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "client/master_client.h"
#include "common/rpc/channel_pool.h"
#include "master.grpc.pb.h"

namespace fluxcache {

namespace {

class FailedPreconditionRmdirService : public proto::MasterService::Service {
 public:
  ::grpc::Status Rmdir(
      ::grpc::ServerContext* /*context*/,
      const ::fluxcache::proto::RmdirRequest* /*request*/,
      ::fluxcache::proto::RmdirResponse* /*response*/) override {
    return ::grpc::Status(::grpc::StatusCode::FAILED_PRECONDITION,
                          "Rmdir: directory not empty");
  }
};

}  // namespace

class MasterClientStatusTest : public ::testing::Test {
 protected:
  void SetUp() override {
    address_ = "127.0.0.1:19723";
    service_ = std::make_unique<FailedPreconditionRmdirService>();

    grpc::ServerBuilder builder;
    builder.AddListeningPort(address_, grpc::InsecureServerCredentials());
    builder.RegisterService(service_.get());
    server_ = builder.BuildAndStart();
    ASSERT_TRUE(server_ != nullptr);
  }

  void TearDown() override {
    if (server_) {
      server_->Shutdown();
      server_->Wait();
    }
  }

  std::string address_;
  std::unique_ptr<FailedPreconditionRmdirService> service_;
  std::unique_ptr<grpc::Server> server_;
};

TEST_F(MasterClientStatusTest,
       RmdirFailedPreconditionDoesNotCollapseToIoError) {
  ChannelPool pool;
  MasterClient client(&pool, address_, ResilienceConfig{});

  Status status = client.Rmdir("/mnt/nonempty");

  EXPECT_FALSE(status.ok());
  EXPECT_NE(status.message().find("directory not empty"), std::string::npos);
  EXPECT_EQ(status.code(), StatusCode::kDirectoryNotEmpty);
}

}  // namespace fluxcache
