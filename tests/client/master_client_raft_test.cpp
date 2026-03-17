#include "client/master_client.h"
#include "common/rpc/channel_pool.h"
#include "master.grpc.pb.h"
#include "master.pb.h"

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace fluxcache {

namespace {

class RedirectingMasterService : public proto::MasterService::Service {
 public:
  explicit RedirectingMasterService(std::string leader_address)
      : leader_address_(std::move(leader_address)) {}

  ::grpc::Status RegisterWorker(
      ::grpc::ServerContext* /*context*/,
      const ::fluxcache::proto::RegisterWorkerRequest* /*request*/,
      ::fluxcache::proto::RegisterWorkerResponse* /*response*/) override {
    return ::grpc::Status(::grpc::StatusCode::UNAVAILABLE,
                          "not leader; leader=" + leader_address_);
  }

 private:
  std::string leader_address_;
};

class LeaderMasterService : public proto::MasterService::Service {
 public:
  ::grpc::Status RegisterWorker(
      ::grpc::ServerContext* /*context*/,
      const ::fluxcache::proto::RegisterWorkerRequest* /*request*/,
      ::fluxcache::proto::RegisterWorkerResponse* response) override {
    response->set_worker_id(1);
    return ::grpc::Status::OK;
  }
};

}  // namespace

class MasterClientRaftTest : public ::testing::Test {
 protected:
  void SetUp() override {
    leader_address_ = "127.0.0.1:19721";
    redirect_address_ = "127.0.0.1:19722";

    redirect_service_ =
        std::make_unique<RedirectingMasterService>(leader_address_);
    leader_service_ = std::make_unique<LeaderMasterService>();

    grpc::ServerBuilder redirect_builder;
    redirect_builder.AddListeningPort(redirect_address_,
                                      grpc::InsecureServerCredentials());
    redirect_builder.RegisterService(redirect_service_.get());
    redirect_server_ = redirect_builder.BuildAndStart();
    ASSERT_TRUE(redirect_server_ != nullptr);

    grpc::ServerBuilder leader_builder;
    leader_builder.AddListeningPort(leader_address_,
                                    grpc::InsecureServerCredentials());
    leader_builder.RegisterService(leader_service_.get());
    leader_server_ = leader_builder.BuildAndStart();
    ASSERT_TRUE(leader_server_ != nullptr);
  }

  void TearDown() override {
    if (redirect_server_) {
      redirect_server_->Shutdown();
      redirect_server_->Wait();
    }
    if (leader_server_) {
      leader_server_->Shutdown();
      leader_server_->Wait();
    }
  }

  std::string leader_address_;
  std::string redirect_address_;
  std::unique_ptr<RedirectingMasterService> redirect_service_;
  std::unique_ptr<LeaderMasterService> leader_service_;
  std::unique_ptr<grpc::Server> redirect_server_;
  std::unique_ptr<grpc::Server> leader_server_;
};

TEST_F(MasterClientRaftTest, RegisterWorkerFollowsLeaderHintFromFollower) {
  ChannelPool pool;
  MasterClient client(&pool, redirect_address_, ResilienceConfig{});

  auto worker_id = client.RegisterWorker("127.0.0.1", 21061);
  ASSERT_TRUE(worker_id.ok()) << worker_id.status().message();
  EXPECT_EQ(worker_id.value(), 1u);
}

}  // namespace fluxcache
