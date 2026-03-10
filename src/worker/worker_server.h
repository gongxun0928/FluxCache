#pragma once

#include "common/config/config.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>

namespace fluxcache {

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

 private:
  WorkerConfig config_;
  std::unique_ptr<WorkerServiceImpl> service_impl_;
  std::unique_ptr<::grpc::Server> server_;
};

}  // namespace fluxcache
