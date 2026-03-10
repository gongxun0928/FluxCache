#include "master/master_service_impl.h"
#include <grpcpp/grpcpp.h>

namespace fluxcache {

::grpc::Status MasterServiceImpl::GetHashRing(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::GetHashRingRequest* /*request*/,
    ::fluxcache::proto::GetHashRingResponse* response) {
  response->set_ring_version(0);
  {
    std::lock_guard<std::mutex> lock(workers_mu_);
    response->clear_workers();
    for (const auto& w : workers_) {
      *response->add_workers() = w;
    }
  }
  return ::grpc::Status::OK;
}

::grpc::Status MasterServiceImpl::RegisterWorker(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::RegisterWorkerRequest* request,
    ::fluxcache::proto::RegisterWorkerResponse* response) {
  if (!request->has_endpoint()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "RegisterWorker: endpoint is required");
  }
  const auto& ep = request->endpoint();
  if (ep.host().empty()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "RegisterWorker: endpoint.host is required");
  }
  if (ep.port() == 0) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "RegisterWorker: endpoint.port is required");
  }

  uint64_t worker_id;
  {
    std::lock_guard<std::mutex> lock(workers_mu_);
    worker_id = next_worker_id_++;
    proto::WorkerEndpoint registered;
    registered.set_worker_id(worker_id);
    registered.set_host(ep.host());
    registered.set_port(ep.port());
    workers_.push_back(registered);
  }

  response->set_worker_id(worker_id);
  return ::grpc::Status::OK;
}

::grpc::Status MasterServiceImpl::GetFileInfo(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::GetFileInfoRequest* /*request*/,
    ::fluxcache::proto::GetFileInfoResponse* /*response*/) {
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "GetFileInfo not implemented");
}

::grpc::Status MasterServiceImpl::CreateFile(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::CreateFileRequest* /*request*/,
    ::fluxcache::proto::CreateFileResponse* /*response*/) {
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "CreateFile not implemented");
}

::grpc::Status MasterServiceImpl::CompleteFile(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::CompleteFileRequest* /*request*/,
    ::fluxcache::proto::CompleteFileResponse* /*response*/) {
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "CompleteFile not implemented");
}

::grpc::Status MasterServiceImpl::DeleteFile(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::DeleteFileRequest* /*request*/,
    ::fluxcache::proto::DeleteFileResponse* /*response*/) {
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "DeleteFile not implemented");
}

::grpc::Status MasterServiceImpl::Mount(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::MountRequest* /*request*/,
    ::fluxcache::proto::MountResponse* /*response*/) {
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Mount not implemented");
}

::grpc::Status MasterServiceImpl::Unmount(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::UnmountRequest* /*request*/,
    ::fluxcache::proto::UnmountResponse* /*response*/) {
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Unmount not implemented");
}

::grpc::Status MasterServiceImpl::ListMounts(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::ListMountsRequest* /*request*/,
    ::fluxcache::proto::ListMountsResponse* /*response*/) {
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "ListMounts not implemented");
}

}  // namespace fluxcache
