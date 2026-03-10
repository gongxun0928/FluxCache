#pragma once

#include "master.grpc.pb.h"
#include <atomic>
#include <mutex>
#include <vector>

namespace fluxcache {

// Minimal implementation of MasterService for P1-05A bootstrap.
// GetHashRing returns ring_version=0, workers=[]; RegisterWorker accepts
// valid requests and returns worker_id; other RPCs return UNIMPLEMENTED.
class MasterServiceImpl : public proto::MasterService::Service {
 public:
  MasterServiceImpl() = default;

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

 private:
  std::atomic<uint64_t> next_worker_id_{1};
  std::mutex workers_mu_;
  std::vector<proto::WorkerEndpoint> workers_;
};

}  // namespace fluxcache
