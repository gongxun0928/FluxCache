#pragma once

#include "common/config/config.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>

namespace fluxcache {

class MasterServiceImpl;

// gRPC server wrapper for Master process. Manages lifecycle: Start, Shutdown.
class MasterServer {
 public:
  explicit MasterServer(const MasterConfig& config);
  ~MasterServer();

  // Start listening on config.host:config.port. Returns false on bind failure.
  bool Start();
  // Gracefully shutdown the server. Safe to call multiple times.
  void Shutdown();

 private:
  MasterConfig config_;
  std::unique_ptr<MasterServiceImpl> service_impl_;
  std::unique_ptr<::grpc::Server> server_;
};

}  // namespace fluxcache
