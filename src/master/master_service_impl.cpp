#include "master/master_service_impl.h"
#include "common/status.h"
#include <chrono>
#include <grpcpp/grpcpp.h>
#include <optional>

namespace fluxcache {

namespace {

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

::grpc::StatusCode ToGrpcCode(StatusCode c) {
  switch (c) {
    case StatusCode::kOk:
      return ::grpc::StatusCode::OK;
    case StatusCode::kNotFound:
      return ::grpc::StatusCode::NOT_FOUND;
    case StatusCode::kInvalidArgument:
      return ::grpc::StatusCode::INVALID_ARGUMENT;
    case StatusCode::kIOError:
      return ::grpc::StatusCode::INTERNAL;
    default:
      return ::grpc::StatusCode::UNKNOWN;
  }
}

}  // namespace

MasterServiceImpl::MasterServiceImpl()
    : hash_ring_manager_([this](WorkerId id) {
        return worker_manager_.GetWorkerState(id);
      }) {}

::grpc::Status MasterServiceImpl::ToGrpcStatus(const Status& s) {
  if (s.ok()) return ::grpc::Status::OK;
  return ::grpc::Status(ToGrpcCode(s.code()), s.message());
}

::grpc::Status MasterServiceImpl::GetHashRing(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::GetHashRingRequest* /*request*/,
    ::fluxcache::proto::GetHashRingResponse* response) {
  auto snap = hash_ring_manager_.GetRingSnapshot(
      [this](WorkerId id) -> std::optional<WorkerEndpointInfo> {
        auto info = worker_manager_.GetWorker(id);
        if (!info) return std::nullopt;
        WorkerEndpointInfo ep;
        ep.worker_id = info->worker_id;
        ep.host = info->host;
        ep.port = info->port;
        return ep;
      });
  response->set_ring_version(snap.ring_version);
  response->clear_workers();
  for (const auto& w : snap.workers) {
    auto* ep = response->add_workers();
    ep->set_worker_id(w.worker_id);
    ep->set_host(w.host);
    ep->set_port(w.port);
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

  int64_t now_ms = NowMs();

  if (ep.worker_id() > 0) {
    bool existing = worker_manager_.RegisterWorker(
        ep.worker_id(), ep.host(), static_cast<uint16_t>(ep.port()), now_ms);
    if (existing) {
      response->set_worker_id(ep.worker_id());
      return ::grpc::Status::OK;
    }
    return ::grpc::Status(::grpc::StatusCode::NOT_FOUND,
                          "RegisterWorker: worker_id not found");
  }

  uint64_t worker_id = next_worker_id_++;
  worker_manager_.RegisterWorker(worker_id, ep.host(),
                                static_cast<uint16_t>(ep.port()), now_ms);
  hash_ring_manager_.AddWorker(worker_id);

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
    const ::fluxcache::proto::MountRequest* request,
    ::fluxcache::proto::MountResponse* /*response*/) {
  if (!request || request->path().empty()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "Mount: path is required");
  }
  if (request->ufs_uri().empty()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "Mount: ufs_uri is required");
  }
  Status s = mount_table_.Mount(request->path(), request->ufs_uri());
  return ToGrpcStatus(s);
}

::grpc::Status MasterServiceImpl::Unmount(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::UnmountRequest* request,
    ::fluxcache::proto::UnmountResponse* /*response*/) {
  if (!request || request->path().empty()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "Unmount: path is required");
  }
  Status s = mount_table_.Unmount(request->path());
  return ToGrpcStatus(s);
}

::grpc::Status MasterServiceImpl::ListMounts(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::ListMountsRequest* /*request*/,
    ::fluxcache::proto::ListMountsResponse* response) {
  if (!response) return ::grpc::Status(::grpc::StatusCode::INTERNAL, "null response");
  auto paths = mount_table_.ListMounts();
  response->clear_paths();
  for (const auto& p : paths) {
    response->add_paths(p);
  }
  return ::grpc::Status::OK;
}

void MasterServiceImpl::CheckWorkerHealthAndUpdateRing(
    int64_t now_ms, int64_t heartbeat_timeout_ms, int64_t suspect_grace_ms) {
  auto newly_dead = worker_manager_.CheckWorkerHealth(
      now_ms, heartbeat_timeout_ms, suspect_grace_ms);
  for (WorkerId wid : newly_dead) {
    hash_ring_manager_.RemoveWorker(wid);
  }
}

}  // namespace fluxcache
