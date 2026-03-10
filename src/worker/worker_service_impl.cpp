#include "worker/worker_service_impl.h"
#include <grpcpp/grpcpp.h>

namespace fluxcache {

::grpc::Status WorkerServiceImpl::ReadPages(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::ReadPagesRequest* /*request*/,
    ::fluxcache::proto::ReadPagesResponse* /*response*/) {
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED,
                        "ReadPages not implemented");
}

::grpc::Status WorkerServiceImpl::WritePages(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::WritePagesRequest* /*request*/,
    ::fluxcache::proto::WritePagesResponse* /*response*/) {
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED,
                        "WritePages not implemented");
}

::grpc::Status WorkerServiceImpl::Heartbeat(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::HeartbeatRequest* /*request*/,
    ::fluxcache::proto::HeartbeatResponse* response) {
  (void)response;
  return ::grpc::Status::OK;
}

}  // namespace fluxcache
