#pragma once

#include "common/metrics/metrics_registry.h"
#include "worker.grpc.pb.h"
#include <atomic>
#include <cstddef>
#include <mutex>

namespace fluxcache {

class PageStore;

// WorkerService implementation. ReadPages uses PageStore + UFS; WritePages
// returns UNIMPLEMENTED; Heartbeat returns empty response.
class WorkerServiceImpl : public proto::WorkerService::Service {
 public:
  WorkerServiceImpl(PageStore* page_store, size_t page_size, size_t block_size,
                    MetricsRegistry* metrics = nullptr,
                    bool allow_stale_read_on_ufs_timeout = false);

  ::grpc::Status ReadPages(::grpc::ServerContext* context,
                           const ::fluxcache::proto::ReadPagesRequest* request,
                           ::fluxcache::proto::ReadPagesResponse* response) override;

  ::grpc::Status BatchReadPages(
      ::grpc::ServerContext* context,
      const ::fluxcache::proto::BatchReadPagesRequest* request,
      ::fluxcache::proto::BatchReadPagesResponse* response) override;

  ::grpc::Status WritePages(::grpc::ServerContext* context,
                            const ::fluxcache::proto::WritePagesRequest* request,
                            ::fluxcache::proto::WritePagesResponse* response) override;

  ::grpc::Status Heartbeat(::grpc::ServerContext* context,
                            const ::fluxcache::proto::HeartbeatRequest* request,
                            ::fluxcache::proto::HeartbeatResponse* response) override;

 private:
  PageStore* page_store_;
  size_t page_size_;
  size_t block_size_;
  MetricsRegistry* metrics_;
  bool allow_stale_read_on_ufs_timeout_;
};

}  // namespace fluxcache
