#include "master/master_service_impl.h"
#include "common/metrics/metrics_registry.h"
#include "common/status.h"
#include "worker.grpc.pb.h"
#ifdef FLUXCACHE_ENABLE_RAFT
#include "master/ha/raft_result.h"
#include "master/ha/raft_node.h"
#include "master.pb.h"
#endif
#include <chrono>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <set>
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
    case StatusCode::kAlreadyExists:
      return ::grpc::StatusCode::ALREADY_EXISTS;
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
      }) {
  if (inode_tree) {
    prewarm_queue_ = std::make_unique<PrewarmQueue>(&path_resolver_);
  }
}

::grpc::Status MasterServiceImpl::ToGrpcStatus(const Status& s) {
  if (s.ok()) return ::grpc::Status::OK;
  return ::grpc::Status(ToGrpcCode(s.code()), s.message());
}

#ifdef FLUXCACHE_ENABLE_RAFT
bool MasterServiceImpl::IsLeader() const {
  if (!raft_node_) return true;
  return raft_node_->IsLeader();
}

::grpc::Status MasterServiceImpl::NotLeaderError() const {
  std::string leader_ep;
  if (raft_node_) leader_ep = raft_node_->GetLeaderEndpoint();
  std::string msg = "not leader";
  if (!leader_ep.empty()) msg += "; leader=" + leader_ep;
  return ::grpc::Status(::grpc::StatusCode::UNAVAILABLE, msg);
}

nuraft::ptr<nuraft::buffer> MasterServiceImpl::ReplicateEntry(
    const proto::JournalEntry& entry) {
  std::string serialized;
  if (!entry.SerializeToString(&serialized)) return nullptr;
  return raft_node_->Replicate(serialized);
}
#endif

// --- Read RPCs ---
// In clustered mode, followers currently serve local replicated state
// (eventual consistency). Linearizable follower reads via ReadIndex/Lease
// Read are intentionally deferred to a follow-up change.

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

#ifdef FLUXCACHE_ENABLE_RAFT
  if (IsRaftEnabled() && !IsLeader()) {
    return NotLeaderError();
  }
#endif

  int64_t now_ms = NowMs();
  uint64_t worker_id = ep.worker_id();

  if (worker_id > 0 && !worker_manager_.GetWorker(worker_id).has_value()) {
    return ::grpc::Status(::grpc::StatusCode::NOT_FOUND,
                          "RegisterWorker: worker_id not found");
  }

#ifdef FLUXCACHE_ENABLE_RAFT
  if (IsRaftEnabled()) {
    if (worker_id == 0) {
      worker_id = next_worker_id_.fetch_add(1);
    }
    proto::JournalEntry je;
    auto* op = je.mutable_upsert_worker();
    op->set_worker_id(worker_id);
    op->set_host(ep.host());
    op->set_port(ep.port());
    op->set_last_heartbeat_ms(now_ms);
    op->set_next_worker_id(std::max<uint64_t>(
        next_worker_id_.load(std::memory_order_relaxed), worker_id + 1));

    auto result = ReplicateEntry(je);
    if (!result) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "RegisterWorker: Raft replication failed");
    }
    auto apply = ParseRaftApplyResult(result, true);
    if (!apply) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "RegisterWorker: invalid Raft apply result");
    }
    if (apply->code != StatusCode::kOk) {
      return ::grpc::Status(ToGrpcCode(apply->code),
                            "RegisterWorker: Raft apply failed");
    }
    response->set_worker_id(apply->value.value_or(worker_id));
    return ::grpc::Status::OK;
  }
#endif

  if (worker_id > 0) {
    bool existing = worker_manager_.RegisterWorker(
        worker_id, ep.host(), static_cast<uint16_t>(ep.port()), now_ms);
    if (existing) {
      response->set_worker_id(worker_id);
      return ::grpc::Status::OK;
    }
    return ::grpc::Status(::grpc::StatusCode::NOT_FOUND,
                          "RegisterWorker: worker_id not found");
  }

  worker_id = next_worker_id_++;
  ApplyReplicatedWorkerRegistration(worker_id, ep.host(),
                                    static_cast<uint16_t>(ep.port()), now_ms,
                                    next_worker_id_.load());
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

  std::optional<InodeId> inode_id;
#ifdef FLUXCACHE_ENABLE_RAFT
  if (IsRaftEnabled()) {
    inode_id = path_resolver_.ResolveLocal(request->path());
  } else {
    inode_id = path_resolver_.ResolveOrSync(request->path());
  }
#else
  inode_id = path_resolver_.ResolveOrSync(request->path());
#endif
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
  fi->set_file_version(entry->file_version);
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

::grpc::Status MasterServiceImpl::SubmitPrewarm(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::SubmitPrewarmRequest* request,
    ::fluxcache::proto::SubmitPrewarmResponse* /*response*/) {
  if (metrics_) metrics_->IncCounter("master", "SubmitPrewarm");
  RpcMetricsGuard _guard(metrics_, "SubmitPrewarm");
  if (!request || request->path().empty()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "SubmitPrewarm: path is required");
  }
  if (!prewarm_queue_) {
    return ::grpc::Status(::grpc::StatusCode::UNAVAILABLE,
                          "SubmitPrewarm: prewarm not available");
  }
  prewarm_queue_->Submit(request->path());
  return ::grpc::Status::OK;
}

// --- Write RPCs (go through Raft when enabled) ---

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

#ifdef FLUXCACHE_ENABLE_RAFT
  if (IsRaftEnabled() && !IsLeader()) {
    return NotLeaderError();
  }
#endif

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

  constexpr uint64_t kDefaultBlockSize = 64ULL * 1024 * 1024;

#ifdef FLUXCACHE_ENABLE_RAFT
  if (IsRaftEnabled()) {
    auto parent_id = inode_tree_->FindParentId(path);
    if (!parent_id.has_value()) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "CreateFile: parent not found");
    }
    auto alloc = inode_tree_->AllocateInodeId();
    int64_t now_ms = NowMs();

    proto::JournalEntry je;
    auto* op = je.mutable_create_file();
    op->set_path(path);
    op->set_inode_id(alloc.id);
    op->set_parent_id(*parent_id);
    op->set_size(0);
    op->set_block_size(kDefaultBlockSize);
    op->set_creation_time_ms(now_ms);
    op->set_mtime_ms(now_ms);
    op->set_next_id(alloc.next_id);

    auto result = ReplicateEntry(je);
    if (!result) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "CreateFile: Raft replication failed");
    }
    auto apply = ParseRaftApplyResult(result, true);
    if (!apply) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "CreateFile: invalid Raft apply result");
    }
    if (apply->code != StatusCode::kOk) {
      return ::grpc::Status(ToGrpcCode(apply->code), "CreateFile: Raft apply failed");
    }

    auto entry = inode_tree_->GetInode(apply->value.value_or(alloc.id));
    if (!entry) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "CreateFile: inode not found after Raft commit");
    }

    auto* fi = response->mutable_file_info();
    fi->set_inode_id(apply->value.value_or(alloc.id));
    fi->set_size(entry->size);
    fi->set_block_size(entry->block_size);
    fi->set_file_version(entry->file_version);
    fi->set_is_directory(false);
    response->set_ufs_uri(ufs_uri);
    response->set_ufs_path(ufs_path);
    return ::grpc::Status::OK;
  }
#endif

  // Standalone path (no Raft)
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
  fi->set_file_version(entry->file_version);
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

#ifdef FLUXCACHE_ENABLE_RAFT
  if (IsRaftEnabled() && !IsLeader()) {
    return NotLeaderError();
  }
#endif

  auto entry = inode_tree_->GetInode(request->inode_id());
  if (!entry) {
    return ::grpc::Status(::grpc::StatusCode::NOT_FOUND,
                          "CompleteFile: inode not found");
  }
  if (entry->is_directory()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "CompleteFile: inode is a directory");
  }

  uint64_t new_file_version = entry->file_version + 1;

#ifdef FLUXCACHE_ENABLE_RAFT
  if (IsRaftEnabled()) {
    proto::JournalEntry je;
    auto* op = je.mutable_complete_file();
    op->set_inode_id(request->inode_id());
    op->set_size(request->size());
    op->set_file_version(new_file_version);

    auto result = ReplicateEntry(je);
    if (!result) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "CompleteFile: Raft replication failed");
    }
    auto apply = ParseRaftApplyResult(result);
    if (!apply) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "CompleteFile: invalid Raft apply result");
    }
    if (apply->code != StatusCode::kOk) {
      return ::grpc::Status(ToGrpcCode(apply->code),
                            "CompleteFile: Raft apply failed");
    }
    return ::grpc::Status::OK;
  }
#endif

  if (!inode_tree_->UpdateInodeSizeAndIncrementVersion(request->inode_id(),
                                                        request->size())) {
    return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                          "CompleteFile: failed to update inode");
  }
  return ::grpc::Status::OK;
}

::grpc::Status MasterServiceImpl::DeleteFile(
    ::grpc::ServerContext* /*context*/,
    const ::fluxcache::proto::DeleteFileRequest* request,
    ::fluxcache::proto::DeleteFileResponse* /*response*/) {
  if (metrics_) metrics_->IncCounter("master", "DeleteFile");
  RpcMetricsGuard _guard(metrics_, "DeleteFile");
  if (!request || request->path().empty()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "DeleteFile: path is required");
  }
  if (!inode_tree_ || !inode_tree_->is_ready()) {
    return ::grpc::Status(::grpc::StatusCode::UNAVAILABLE,
                          "DeleteFile: InodeTree not ready");
  }

#ifdef FLUXCACHE_ENABLE_RAFT
  if (IsRaftEnabled() && !IsLeader()) {
    return NotLeaderError();
  }
#endif

  auto inode_id = path_resolver_.ResolveOrSync(request->path());
  if (!inode_id.has_value()) {
    return ::grpc::Status(::grpc::StatusCode::NOT_FOUND,
                          "DeleteFile: path not found");
  }

  auto entry = inode_tree_->GetInode(*inode_id);
  if (!entry.has_value()) {
    return ::grpc::Status(::grpc::StatusCode::NOT_FOUND,
                          "DeleteFile: inode not found");
  }
  if (entry->is_directory()) {
    return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT,
                          "DeleteFile: cannot delete directory");
  }

#ifdef FLUXCACHE_ENABLE_RAFT
  if (IsRaftEnabled()) {
    proto::JournalEntry je;
    auto* op = je.mutable_delete_file();
    op->set_inode_id(*inode_id);
    op->set_path(request->path());

    auto result = ReplicateEntry(je);
    if (!result) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "DeleteFile: Raft replication failed");
    }
    auto apply = ParseRaftApplyResult(result);
    if (!apply) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "DeleteFile: invalid Raft apply result");
    }
    if (apply->code != StatusCode::kOk) {
      return ::grpc::Status(ToGrpcCode(apply->code),
                            "DeleteFile: Raft apply failed");
    }
    AddPendingOrphan(*inode_id);
    return ::grpc::Status::OK;
  }
#endif

  if (!inode_tree_->DeleteInode(*inode_id)) {
    return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                          "DeleteFile: failed to delete inode");
  }

  AddPendingOrphan(*inode_id);
  return ::grpc::Status::OK;
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

#ifdef FLUXCACHE_ENABLE_RAFT
  if (IsRaftEnabled() && !IsLeader()) {
    return NotLeaderError();
  }
  if (IsRaftEnabled()) {
    proto::JournalEntry je;
    auto* op = je.mutable_mount_op();
    op->set_path(request->path());
    op->set_ufs_uri(request->ufs_uri());

    auto result = ReplicateEntry(je);
    if (!result) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "Mount: Raft replication failed");
    }
    auto apply = ParseRaftApplyResult(result);
    if (!apply) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "Mount: invalid Raft apply result");
    }
    if (apply->code != StatusCode::kOk) {
      return ::grpc::Status(ToGrpcCode(apply->code), "Mount: Raft apply failed");
    }
    return ::grpc::Status::OK;
  }
#endif

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

#ifdef FLUXCACHE_ENABLE_RAFT
  if (IsRaftEnabled() && !IsLeader()) {
    return NotLeaderError();
  }
  if (IsRaftEnabled()) {
    proto::JournalEntry je;
    auto* op = je.mutable_unmount_op();
    op->set_path(request->path());

    auto result = ReplicateEntry(je);
    if (!result) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "Unmount: Raft replication failed");
    }
    auto apply = ParseRaftApplyResult(result);
    if (!apply) {
      return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                            "Unmount: invalid Raft apply result");
    }
    if (apply->code != StatusCode::kOk) {
      return ::grpc::Status(ToGrpcCode(apply->code),
                            "Unmount: Raft apply failed");
    }
    return ::grpc::Status::OK;
  }
#endif

  Status s = mount_table_.Unmount(request->path());
  return ToGrpcStatus(s);
}

// --- Non-RPC methods ---

void MasterServiceImpl::CheckWorkerHealthAndUpdateRing(
    int64_t now_ms, int64_t heartbeat_timeout_ms, int64_t suspect_grace_ms) {
#ifdef FLUXCACHE_ENABLE_RAFT
  if (IsRaftEnabled()) {
    if (!IsLeader()) return;
    std::vector<WorkerStateTransition> transitions;
    const int64_t recovered_at_ms =
        topology_recovered_at_ms_.load(std::memory_order_relaxed);
    if (recovered_at_ms > 0) {
      for (const auto& info : worker_manager_.GetAllWorkers()) {
        if (info.state == WorkerState::kDead) continue;
        if ((now_ms - info.last_heartbeat_ms) < heartbeat_timeout_ms) continue;
        if (info.state == WorkerState::kAlive ||
            (info.state == WorkerState::kSuspect &&
             info.suspect_since_ms < recovered_at_ms)) {
          transitions.push_back(WorkerStateTransition{
              info.worker_id, WorkerState::kSuspect, info.last_heartbeat_ms,
              now_ms});
        }
      }
    }
    if (transitions.empty()) {
      transitions = worker_manager_.CollectHealthTransitions(
          now_ms, heartbeat_timeout_ms, suspect_grace_ms);
    }
    for (const auto& transition : transitions) {
      proto::JournalEntry je;
      auto* op = je.mutable_update_worker_state();
      op->set_worker_id(transition.worker_id);
      op->set_state(static_cast<uint32_t>(transition.state));
      op->set_last_heartbeat_ms(transition.last_heartbeat_ms);
      op->set_suspect_since_ms(transition.suspect_since_ms);
      auto result = ReplicateEntry(je);
      if (!result) {
        std::cerr << "CheckWorkerHealthAndUpdateRing: failed to replicate "
                  << "state transition for worker " << transition.worker_id
                  << "\n";
        continue;
      }
      auto apply = ParseRaftApplyResult(result);
      if (!apply || apply->code != StatusCode::kOk) {
        std::cerr << "CheckWorkerHealthAndUpdateRing: failed to apply state "
                  << "transition for worker " << transition.worker_id << "\n";
      }
    }
    return;
  }
#endif
  auto newly_dead = worker_manager_.CheckWorkerHealth(
      now_ms, heartbeat_timeout_ms, suspect_grace_ms);
  for (WorkerId wid : newly_dead) {
    hash_ring_manager_.RemoveWorker(wid);
  }
}

void MasterServiceImpl::SetWorkerHealthPolicy(int64_t heartbeat_timeout_ms,
                                              int64_t suspect_grace_ms) {
  worker_heartbeat_timeout_ms_.store(heartbeat_timeout_ms,
                                     std::memory_order_relaxed);
  worker_suspect_grace_ms_.store(suspect_grace_ms, std::memory_order_relaxed);
}

void MasterServiceImpl::AddPendingOrphan(InodeId inode_id) {
  std::lock_guard<std::mutex> lock(orphan_mu_);
  pending_orphan_inodes_.insert(static_cast<uint64_t>(inode_id));
}

void MasterServiceImpl::RemovePendingOrphans(
    const std::vector<uint64_t>& audit_inode_ids) {
  std::lock_guard<std::mutex> lock(orphan_mu_);
  for (uint64_t id : audit_inode_ids) {
    pending_orphan_inodes_.erase(id);
  }
}

void MasterServiceImpl::RunHeartbeatToAllWorkers() {
#ifdef FLUXCACHE_ENABLE_RAFT
  if (IsRaftEnabled() && !IsLeader()) return;
#endif
  auto workers = worker_manager_.GetAllWorkersInRing();
  for (const auto& w : workers) {
    if (w.state == WorkerState::kAlive) {
      CallWorkerHeartbeat(w.worker_id, w.host, w.port);
    }
  }
  UpdateActiveWorkersGauge();
}

void MasterServiceImpl::UpdateActiveWorkersGauge() {
  if (!metrics_) return;
  auto workers = worker_manager_.GetAllWorkersInRing();
  metrics_->SetGauge("fluxcache_active_workers",
                    static_cast<uint64_t>(workers.size()));
}

void MasterServiceImpl::CallWorkerHeartbeat(WorkerId worker_id,
                                            const std::string& host,
                                            uint16_t port) {
  std::string addr = host + ":" + std::to_string(port);
  auto channel =
      grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
  fluxcache::proto::WorkerService::Stub stub(channel);

  fluxcache::proto::HeartbeatRequest req;
  req.set_worker_id(worker_id);
  {
    std::lock_guard<std::mutex> lock(orphan_mu_);
    for (uint64_t id : pending_orphan_inodes_) {
      req.add_orphan_inode_ids(id);
    }
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
  for (const auto& w : snap.workers) {
    auto* ep = req.add_ring_workers();
    ep->set_worker_id(w.worker_id);
    ep->set_host(w.host);
    ep->set_port(w.port);
  }

  fluxcache::proto::HeartbeatResponse resp;
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
  auto status = stub.Heartbeat(&ctx, req, &resp);

  if (status.ok()) {
    RemovePendingOrphans(
        std::vector<uint64_t>(resp.audit_inode_ids().begin(),
                              resp.audit_inode_ids().end()));
  }
}

void MasterServiceImpl::BindMountTableStore(InodeStore* store) {
  mount_table_.BindStore(store);
}

void MasterServiceImpl::RecoverMountTable() {
  mount_table_.RecoverFromStore();
}

Status MasterServiceImpl::ApplyReplicatedWorkerRegistration(
    WorkerId worker_id, const std::string& host, uint16_t port,
    int64_t last_heartbeat_ms, uint64_t next_worker_id) {
  std::lock_guard<std::mutex> topology_lock(topology_mu_);
  worker_manager_.ApplyWorkerRegistration(worker_id, host, port, last_heartbeat_ms);
  if (!hash_ring_manager_.ContainsWorker(worker_id)) {
    hash_ring_manager_.AddWorker(worker_id);
  }
  uint64_t current = next_worker_id_.load(std::memory_order_relaxed);
  while (current < next_worker_id &&
         !next_worker_id_.compare_exchange_weak(
             current, next_worker_id, std::memory_order_relaxed)) {
  }
  return Status::OK();
}

Status MasterServiceImpl::ApplyReplicatedWorkerState(
    WorkerId worker_id, WorkerState state, int64_t last_heartbeat_ms,
    int64_t suspect_since_ms) {
  std::lock_guard<std::mutex> topology_lock(topology_mu_);
  if (!worker_manager_.ApplyWorkerState(worker_id, state, last_heartbeat_ms,
                                        suspect_since_ms)) {
    return Status::NotFound("ApplyReplicatedWorkerState: worker not found");
  }
  bool in_ring = hash_ring_manager_.ContainsWorker(worker_id);
  if (state == WorkerState::kDead) {
    if (in_ring) hash_ring_manager_.RemoveWorker(worker_id);
  } else if (!in_ring) {
    hash_ring_manager_.AddWorker(worker_id);
  }
  return Status::OK();
}

void MasterServiceImpl::BuildWorkerTopologySnapshot(
    proto::WorkerTopologySnapshot* snapshot) const {
  if (!snapshot) return;
  std::lock_guard<std::mutex> topology_lock(topology_mu_);
  snapshot->Clear();
  snapshot->set_next_worker_id(next_worker_id_.load(std::memory_order_relaxed));
  snapshot->set_ring_version(hash_ring_manager_.GetVersion());
  for (const auto& info : worker_manager_.GetAllWorkers()) {
    auto* worker = snapshot->add_workers();
    worker->set_worker_id(info.worker_id);
    worker->set_host(info.host);
    worker->set_port(info.port);
    worker->set_state(static_cast<uint32_t>(info.state));
    worker->set_last_heartbeat_ms(info.last_heartbeat_ms);
    worker->set_suspect_since_ms(info.suspect_since_ms);
  }
}

bool MasterServiceImpl::RestoreWorkerTopologySnapshot(
    const proto::WorkerTopologySnapshot& snapshot) {
  std::lock_guard<std::mutex> topology_lock(topology_mu_);
  std::vector<WorkerInfo> workers;
  workers.reserve(snapshot.workers_size());
  for (const auto& entry : snapshot.workers()) {
    WorkerInfo info;
    info.worker_id = entry.worker_id();
    info.host = entry.host();
    info.port = static_cast<uint16_t>(entry.port());
    info.state = static_cast<WorkerState>(entry.state());
    info.last_heartbeat_ms = entry.last_heartbeat_ms();
    info.suspect_since_ms = entry.suspect_since_ms();
    workers.push_back(info);
  }
  worker_manager_.ReplaceAllWorkers(workers);
  hash_ring_manager_.RestoreFromWorkers(workers, snapshot.ring_version());
  next_worker_id_.store(std::max<uint64_t>(snapshot.next_worker_id(), 1),
                        std::memory_order_relaxed);
  topology_recovered_at_ms_.store(NowMs(), std::memory_order_relaxed);
  return true;
}

}  // namespace fluxcache
