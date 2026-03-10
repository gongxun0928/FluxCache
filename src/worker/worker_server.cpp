#include "worker/worker_server.h"
#include "worker/worker_service_impl.h"
#include <grpcpp/grpcpp.h>

namespace fluxcache {

WorkerServer::WorkerServer(const WorkerConfig& config) : config_(config) {
  service_impl_ = std::make_unique<WorkerServiceImpl>();
}

WorkerServer::~WorkerServer() {
  Shutdown();
}

bool WorkerServer::Start() {
  std::string addr = config_.host + ":" + std::to_string(config_.port);
  ::grpc::ServerBuilder builder;
  builder.AddListeningPort(addr, ::grpc::InsecureServerCredentials());
  builder.RegisterService(service_impl_.get());
  server_ = builder.BuildAndStart();
  return server_ != nullptr;
}

void WorkerServer::Shutdown() {
  if (server_) {
    server_->Shutdown();
    server_->Wait();
    server_.reset();
  }
}

}  // namespace fluxcache
