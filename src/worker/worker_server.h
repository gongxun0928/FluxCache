#pragma once

#include "common/config/config.h"
#include "worker/page/page_store.h"
#include "worker/storage/memory_tier.h"
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
  static constexpr size_t kPageSize = 1024 * 1024;    // 1MB
  static constexpr size_t kBlockSize = 64ULL * 1024 * 1024;  // 64MB

  WorkerConfig config_;
  std::unique_ptr<MemoryTier> memory_tier_;
  std::unique_ptr<PageStore> page_store_;
  std::unique_ptr<WorkerServiceImpl> service_impl_;
  std::unique_ptr<::grpc::Server> server_;
};

}  // namespace fluxcache
