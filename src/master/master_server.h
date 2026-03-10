#pragma once

#include "common/config/config.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>

namespace fluxcache {

class InodeTree;
class MasterServiceImpl;

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

 private:
  MasterConfig config_;
  std::unique_ptr<InodeTree> inode_tree_;
  std::unique_ptr<MasterServiceImpl> service_impl_;
  std::unique_ptr<::grpc::Server> server_;
};

}  // namespace fluxcache
