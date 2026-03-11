#pragma once

#include "common/config/config.h"
#include "common/metrics/slow_request_tracker.h"
#include "worker/cache/eviction_policy.h"
#include "worker/cache/hotspot_tracker.h"
#include "worker/page/page_store.h"
#include "worker/storage/tier_manager.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>

namespace fluxcache {

class HttpMetricsServer;
class MetricsRegistry;
class WorkerServiceImpl;

// gRPC server wrapper for Worker process. Manages lifecycle: Start, Shutdown.
class WorkerServer {
 public:
  explicit WorkerServer(const WorkerConfig& config);
  ~WorkerServer();

  // Start listening on config.host:config.port. Returns false on bind failure.
  bool Start();
  // Gracefully shutdown the server. Safe to call multiple times.
  void Shutdown();

  // For testing: access PageStore to verify GC.
  PageStore* page_store() { return page_store_.get(); }

 private:
  static constexpr size_t kPageSize = 1024 * 1024;    // 1MB
  static constexpr size_t kBlockSize = 64ULL * 1024 * 1024;  // 64MB
  static constexpr size_t kSsdCapacity = 1024ULL * 1024 * 1024;  // 1GB

  WorkerConfig config_;
  std::unique_ptr<TierManager> tier_manager_;
  std::unique_ptr<class MetaStore> meta_store_;
  std::unique_ptr<EvictionPolicy> eviction_policy_;
  std::unique_ptr<PageStore> page_store_;
  std::unique_ptr<MetricsRegistry> metrics_registry_;
  std::unique_ptr<SlowRequestTracker> slow_request_tracker_;
  std::unique_ptr<HotspotTracker> hotspot_tracker_;
  std::unique_ptr<WorkerServiceImpl> service_impl_;
  std::unique_ptr<HttpMetricsServer> http_metrics_server_;
  std::unique_ptr<::grpc::Server> server_;
};

}  // namespace fluxcache
