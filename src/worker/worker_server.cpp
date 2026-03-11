#include "worker/worker_server.h"
#include "worker/worker_service_impl.h"
#include "common/metrics/http_metrics_server.h"
#include "common/metrics/metrics_registry.h"
#include <grpcpp/grpcpp.h>

namespace fluxcache {

WorkerServer::WorkerServer(const WorkerConfig& config) : config_(config) {
  memory_tier_ = std::make_unique<MemoryTier>(256 * kPageSize);  // 256MB
  page_store_ = std::make_unique<PageStore>(memory_tier_.get(), kPageSize);
  if (config_.metrics_port > 0) {
    metrics_registry_ = std::make_unique<MetricsRegistry>();
    service_impl_ = std::make_unique<WorkerServiceImpl>(
        page_store_.get(), kPageSize, kBlockSize, metrics_registry_.get());
    http_metrics_server_ =
        std::make_unique<HttpMetricsServer>(config_.metrics_port,
                                            metrics_registry_.get());
  } else {
    service_impl_ = std::make_unique<WorkerServiceImpl>(
        page_store_.get(), kPageSize, kBlockSize, nullptr);
  }
}

WorkerServer::~WorkerServer() {
  Shutdown();
}

bool WorkerServer::Start() {
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

void WorkerServer::Shutdown() {
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
