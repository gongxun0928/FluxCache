#pragma once

#include "worker.grpc.pb.h"
#include <atomic>
#include <mutex>

namespace fluxcache {

// Minimal implementation of WorkerService for P1-06 skeleton.
// ReadPages/WritePages return UNIMPLEMENTED; Heartbeat returns empty response.
class WorkerServiceImpl : public proto::WorkerService::Service {
 public:
  WorkerServiceImpl() = default;

  ::grpc::Status ReadPages(::grpc::ServerContext* context,
                           const ::fluxcache::proto::ReadPagesRequest* request,
                           ::fluxcache::proto::ReadPagesResponse* response) override;

  ::grpc::Status WritePages(::grpc::ServerContext* context,
                            const ::fluxcache::proto::WritePagesRequest* request,
                            ::fluxcache::proto::WritePagesResponse* response) override;

  ::grpc::Status Heartbeat(::grpc::ServerContext* context,
                            const ::fluxcache::proto::HeartbeatRequest* request,
                            ::fluxcache::proto::HeartbeatResponse* response) override;
};

}  // namespace fluxcache
