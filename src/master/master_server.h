#pragma once

#include "common/config/config.h"
#include <grpcpp/grpcpp.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace fluxcache {

class HttpMetricsServer;
class InodeTree;
class MasterServiceImpl;
class MetricsRegistry;

// gRPC server wrapper for Master process. Manages lifecycle: Start, Shutdown.
class MasterServer {
 public:
  explicit MasterServer(const MasterConfig& config);
  ~MasterServer();

  // Start listening on config.host:config.port. Returns false on bind failure.
  // Initializes InodeTree (root or recover) before starting gRPC.
  bool Start();
  // Gracefully shutdown the server. Safe to call multiple times.
  void Shutdown();

  InodeTree* inode_tree() { return inode_tree_.get(); }

  MasterServiceImpl* service_impl() { return service_impl_.get(); }

 private:
  MasterConfig config_;
  std::unique_ptr<InodeTree> inode_tree_;
  std::unique_ptr<MetricsRegistry> metrics_registry_;
  std::unique_ptr<MasterServiceImpl> service_impl_;
  std::unique_ptr<HttpMetricsServer> http_metrics_server_;
  std::unique_ptr<::grpc::Server> server_;
  std::atomic<bool> heartbeat_stop_{false};
  std::thread heartbeat_thread_;
};

}  // namespace fluxcache
