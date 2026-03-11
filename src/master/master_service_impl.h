#pragma once

#include "common/metrics/metrics_registry.h"
#include "common/status.h"
#include "master.grpc.pb.h"
#include "master/hash_ring_manager.h"
#include "master/inode_tree.h"
#include "master/mount_table.h"
#include "master/path_resolver.h"
#include "master/worker_manager.h"
#include <atomic>
#include <memory>

namespace fluxcache {

// MasterService implementation with WorkerManager and HashRingManager (P1-05C).
class MasterServiceImpl : public proto::MasterService::Service {
 public:
  explicit MasterServiceImpl(InodeTree* inode_tree = nullptr,
                            MetricsRegistry* metrics = nullptr);

  ::grpc::Status GetHashRing(::grpc::ServerContext* context,
                             const ::fluxcache::proto::GetHashRingRequest* request,
                             ::fluxcache::proto::GetHashRingResponse* response) override;

  ::grpc::Status RegisterWorker(::grpc::ServerContext* context,
                                const ::fluxcache::proto::RegisterWorkerRequest* request,
                                ::fluxcache::proto::RegisterWorkerResponse* response) override;

  // Unimplemented RPCs for Phase 1 bootstrap
  ::grpc::Status GetFileInfo(::grpc::ServerContext* context,
                             const ::fluxcache::proto::GetFileInfoRequest* request,
                             ::fluxcache::proto::GetFileInfoResponse* response) override;
  ::grpc::Status CreateFile(::grpc::ServerContext* context,
                            const ::fluxcache::proto::CreateFileRequest* request,
                            ::fluxcache::proto::CreateFileResponse* response) override;
  ::grpc::Status CompleteFile(::grpc::ServerContext* context,
                              const ::fluxcache::proto::CompleteFileRequest* request,
                              ::fluxcache::proto::CompleteFileResponse* response) override;
  ::grpc::Status DeleteFile(::grpc::ServerContext* context,
                            const ::fluxcache::proto::DeleteFileRequest* request,
                            ::fluxcache::proto::DeleteFileResponse* response) override;
  ::grpc::Status Mount(::grpc::ServerContext* context,
                       const ::fluxcache::proto::MountRequest* request,
                       ::fluxcache::proto::MountResponse* response) override;
  ::grpc::Status Unmount(::grpc::ServerContext* context,
                         const ::fluxcache::proto::UnmountRequest* request,
                         ::fluxcache::proto::UnmountResponse* response) override;
  ::grpc::Status ListMounts(::grpc::ServerContext* context,
                           const ::fluxcache::proto::ListMountsRequest* request,
                           ::fluxcache::proto::ListMountsResponse* response) override;

  void CheckWorkerHealthAndUpdateRing(int64_t now_ms,
                                      int64_t heartbeat_timeout_ms,
                                      int64_t suspect_grace_ms);

 private:
  static ::grpc::Status ToGrpcStatus(const Status& s);

  InodeTree* inode_tree_;
  MetricsRegistry* metrics_;
  PathResolver path_resolver_;
  std::atomic<uint64_t> next_worker_id_{1};
  WorkerManager worker_manager_;
  HashRingManager hash_ring_manager_;
  MountTable mount_table_;
};

}  // namespace fluxcache
