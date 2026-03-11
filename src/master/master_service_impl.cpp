#include "master/master_service_impl.h"
#include "common/metrics/metrics_registry.h"
#include "common/status.h"
#include <chrono>
#include <grpcpp/grpcpp.h>
#include <optional>
#include <string>

namespace fluxcache {

namespace {

struct RpcMetricsGuard {
  MetricsRegistry* m;
  std::string method;
  std::chrono::steady_clock::time_point start;
  RpcMetricsGuard(MetricsRegistry* m, const char* method)
      : m(m), method(method), start(std::chrono::steady_clock::now()) {}
  ~RpcMetricsGuard() {
    if (m) {
      auto sec = std::chrono::duration<double>(
                     std::chrono::steady_clock::now() - start)
                     .count();
      m->ObserveLatency("master", method, sec);
    }
  }
};

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
    case StatusCode::kResourceExhausted:
      return ::grpc::StatusCode::RESOURCE_EXHAUSTED;
    default:
      return ::grpc::StatusCode::UNKNOWN;
  }
}

std::string Dirname(const std::string& path) {
  if (path.empty() || path == "/") return "/";
  size_t pos = path.rfind('/');
  if (pos == std::string::npos) return "/";
  if (pos == 0) return "/";
  return path.substr(0, pos);
}

}  // namespace

MasterServiceImpl::MasterServiceImpl(InodeTree* inode_tree,
                                     MetricsRegistry* metrics)
    : inode_tree_(inode_tree),
      metrics_(metrics),
      path_resolver_(inode_tree, &mount_table_),
      hash_ring_manager_([this](WorkerId id) {
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
  if (metrics_) metrics_->IncCounter("master", "GetHashRing");
  RpcMetricsGuard _guard(metrics_, "GetHashRing");
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
  if (metrics_) metrics_->IncCounter("master", "RegisterWorker");
  RpcMetricsGuard _guard(metrics_, "RegisterWorker");
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
    const ::fluxcache::proto::GetFileInfoRequest* request,
    ::fluxcache::proto::GetFileInfoResponse* response) {
  if (metrics_) metrics_->IncCounter("master", "GetFileInfo");
  RpcMetricsGuard _guard(metrics_, "GetFileInfo");
  if (!request || request->path().empty()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "GetFileInfo: path is required");
  }
  if (!response) {
    return ::grpc::Status(::grpc::StatusCode::INTERNAL, "null response");
  }
  if (!inode_tree_ || !inode_tree_->is_ready()) {
    return ::grpc::Status(::grpc::StatusCode::UNAVAILABLE,
                          "GetFileInfo: InodeTree not ready");
  }

  auto inode_id = path_resolver_.ResolveOrSync(request->path());
  if (!inode_id.has_value()) {
    return ::grpc::Status(::grpc::StatusCode::NOT_FOUND,
                          "GetFileInfo: path not found");
  }

  auto entry = inode_tree_->GetInode(*inode_id);
  if (!entry.has_value()) {
    return ::grpc::Status(::grpc::StatusCode::NOT_FOUND,
                          "GetFileInfo: inode not found");
  }

  std::string ufs_uri, ufs_path;
  Status s = mount_table_.Resolve(request->path(), &ufs_uri, &ufs_path);
  if (!s.ok()) {
    return ToGrpcStatus(s);
  }

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

  auto* fi = response->mutable_file_info();
  fi->set_inode_id(*inode_id);
  fi->set_size(entry->size);
  fi->set_block_size(entry->block_size);
  fi->set_ufs_mtime_ms(entry->modification_time_ms);
  fi->set_is_directory(entry->is_directory());
  response->set_ring_version(snap.ring_version);
  response->set_ufs_uri(ufs_uri);
  response->set_ufs_path(ufs_path);
  for (const auto& w : snap.workers) {
    auto* ep = response->add_workers();
    ep->set_worker_id(w.worker_id);
    ep->set_host(w.host);
    ep->set_port(w.port);
  }
  return ::grpc::Status::OK;
}

::grpc::Status MasterServiceImpl::CreateFile(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::CreateFileRequest* request,
    ::fluxcache::proto::CreateFileResponse* response) {
  if (metrics_) metrics_->IncCounter("master", "CreateFile");
  RpcMetricsGuard _guard(metrics_, "CreateFile");
  if (!request || request->path().empty()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "CreateFile: path is required");
  }
  const std::string& path = request->path();
  if (path[0] != '/' || path == "/") {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "CreateFile: path must be absolute and not root");
  }
  if (!inode_tree_ || !inode_tree_->is_ready()) {
    return ::grpc::Status(::grpc::StatusCode::UNAVAILABLE,
                          "CreateFile: InodeTree not ready");
  }

  std::string ufs_uri, ufs_path;
  Status s = mount_table_.Resolve(path, &ufs_uri, &ufs_path);
  if (!s.ok()) {
    return ::grpc::Status(::grpc::StatusCode::NOT_FOUND,
                          "CreateFile: path not under any mount point");
  }

  s = path_resolver_.SyncFromUfs(Dirname(path));
  if (!s.ok()) {
    return ToGrpcStatus(s);
  }

  if (inode_tree_->LookupPath(path).has_value()) {
    return ::grpc::Status(::grpc::StatusCode::ALREADY_EXISTS,
                          "CreateFile: path already exists");
  }

  constexpr uint64_t kDefaultBlockSize = 64ULL * 1024 * 1024;  // 64MB
  auto inode_id =
      inode_tree_->CreateFile(path, 0, kDefaultBlockSize, NowMs());
  if (!inode_id.has_value()) {
    return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                          "CreateFile: failed to create inode");
  }

  auto entry = inode_tree_->GetInode(*inode_id);
  if (!entry) {
    return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                          "CreateFile: inode not found after create");
  }

  auto* fi = response->mutable_file_info();
  fi->set_inode_id(*inode_id);
  fi->set_size(entry->size);
  fi->set_block_size(entry->block_size);
  fi->set_ufs_mtime_ms(entry->modification_time_ms);
  fi->set_is_directory(false);
  response->set_ufs_uri(ufs_uri);
  response->set_ufs_path(ufs_path);
  return ::grpc::Status::OK;
}

::grpc::Status MasterServiceImpl::CompleteFile(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::CompleteFileRequest* request,
    ::fluxcache::proto::CompleteFileResponse* /*response*/) {
  if (metrics_) metrics_->IncCounter("master", "CompleteFile");
  RpcMetricsGuard _guard(metrics_, "CompleteFile");
  if (!request || request->inode_id() == 0) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "CompleteFile: inode_id is required");
  }
  if (!inode_tree_ || !inode_tree_->is_ready()) {
    return ::grpc::Status(::grpc::StatusCode::UNAVAILABLE,
                          "CompleteFile: InodeTree not ready");
  }

  auto entry = inode_tree_->GetInode(request->inode_id());
  if (!entry) {
    return ::grpc::Status(::grpc::StatusCode::NOT_FOUND,
                          "CompleteFile: inode not found");
  }
  if (entry->is_directory()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "CompleteFile: inode is a directory");
  }

  int64_t mtime_ms = request->has_ufs_mtime_ms()
                         ? request->ufs_mtime_ms()
                         : entry->modification_time_ms;
  if (!inode_tree_->UpdateInodeSizeAndMtime(request->inode_id(),
                                            request->size(), mtime_ms)) {
    return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                          "CompleteFile: failed to update inode");
  }
  return ::grpc::Status::OK;
}

::grpc::Status MasterServiceImpl::DeleteFile(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::DeleteFileRequest* /*request*/,
    ::fluxcache::proto::DeleteFileResponse* /*response*/) {
  if (metrics_) metrics_->IncCounter("master", "DeleteFile");
  RpcMetricsGuard _guard(metrics_, "DeleteFile");
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "DeleteFile not implemented");
}

::grpc::Status MasterServiceImpl::Mount(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::MountRequest* request,
    ::fluxcache::proto::MountResponse* /*response*/) {
  if (metrics_) metrics_->IncCounter("master", "Mount");
  RpcMetricsGuard _guard(metrics_, "Mount");
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
  if (metrics_) metrics_->IncCounter("master", "Unmount");
  RpcMetricsGuard _guard(metrics_, "Unmount");
  if (!request || request->path().empty()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "Unmount: path is required");
  }
  if (inode_tree_ && inode_tree_->HasInodesUnderPath(request->path())) {
    return ::grpc::Status(::grpc::StatusCode::FAILED_PRECONDITION,
                          "Unmount: mount point has active inodes or discovered children");
  }
  Status s = mount_table_.Unmount(request->path());
  return ToGrpcStatus(s);
}

::grpc::Status MasterServiceImpl::ListMounts(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::ListMountsRequest* /*request*/,
    ::fluxcache::proto::ListMountsResponse* response) {
  if (metrics_) metrics_->IncCounter("master", "ListMounts");
  RpcMetricsGuard _guard(metrics_, "ListMounts");
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
