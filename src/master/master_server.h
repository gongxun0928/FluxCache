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
#ifdef FLUXCACHE_ENABLE_RAFT
class RaftNode;
#endif

class MasterServer {
 public:
  explicit MasterServer(const MasterConfig& config);
  ~MasterServer();

  bool Start();
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
#ifdef FLUXCACHE_ENABLE_RAFT
  std::unique_ptr<RaftNode> raft_node_;
#endif
};

}  // namespace fluxcache
