#include "master/master_server.h"
#include "master/master_service_impl.h"
#include <grpcpp/grpcpp.h>

namespace fluxcache {

MasterServer::MasterServer(const MasterConfig& config) : config_(config) {
  service_impl_ = std::make_unique<MasterServiceImpl>();
}

MasterServer::~MasterServer() {
  Shutdown();
}

bool MasterServer::Start() {
  std::string addr = config_.host + ":" + std::to_string(config_.port);
  ::grpc::ServerBuilder builder;
  builder.AddListeningPort(addr, ::grpc::InsecureServerCredentials());
  builder.RegisterService(service_impl_.get());
  server_ = builder.BuildAndStart();
  return server_ != nullptr;
}

void MasterServer::Shutdown() {
  if (server_) {
    server_->Shutdown();
    server_->Wait();
    server_.reset();
  }
}

}  // namespace fluxcache
