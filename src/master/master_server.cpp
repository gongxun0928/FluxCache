#include "master/inode_tree.h"
#include "master/master_server.h"
#include "master/master_service_impl.h"
#include "common/metrics/http_metrics_server.h"
#include "common/metrics/metrics_registry.h"
#include <grpcpp/grpcpp.h>

namespace fluxcache {

MasterServer::MasterServer(const MasterConfig& config) : config_(config) {
  inode_tree_ = std::make_unique<InodeTree>(config_.db_path);
  if (config_.metrics_port > 0) {
    metrics_registry_ = std::make_unique<MetricsRegistry>();
    service_impl_ = std::make_unique<MasterServiceImpl>(inode_tree_.get(),
                                                          metrics_registry_.get());
    http_metrics_server_ =
        std::make_unique<HttpMetricsServer>(config_.metrics_port,
                                            metrics_registry_.get());
  } else {
    service_impl_ = std::make_unique<MasterServiceImpl>(inode_tree_.get(),
                                                          nullptr);
  }
}

MasterServer::~MasterServer() {
  Shutdown();
}

bool MasterServer::Start() {
  if (!inode_tree_->InitOrRecover()) {
    return false;
  }
  if (http_metrics_server_ && !http_metrics_server_->Start()) {
    return false;
  }
  std::string addr = config_.host + ":" + std::to_string(config_.port);
  ::grpc::ServerBuilder builder;
  builder.AddListeningPort(addr, ::grpc::InsecureServerCredentials());
  builder.RegisterService(service_impl_.get());
  server_ = builder.BuildAndStart();
  return server_ != nullptr;
}

void MasterServer::Shutdown() {
  if (http_metrics_server_) {
    http_metrics_server_->Shutdown();
  }
  if (server_) {
    server_->Shutdown();
    server_->Wait();
    server_.reset();
  }
}

}  // namespace fluxcache
