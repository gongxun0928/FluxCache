#pragma once

#include "common/metrics/metrics_registry.h"
#include "common/status.h"
#include "master.grpc.pb.h"
#include "master/hash_ring_manager.h"
#include "master/inode_tree.h"
#include "master/mount_table.h"
#include "master/path_resolver.h"
#include "master/prewarm_queue.h"
#include "master/worker_manager.h"
#ifdef FLUXCACHE_ENABLE_RAFT
#include "libnuraft/nuraft.hxx"
#endif
#include <atomic>
#include <memory>
#include <mutex>
#include <set>

namespace fluxcache {

class InodeStore;
#ifdef FLUXCACHE_ENABLE_RAFT
class RaftNode;
namespace proto { class JournalEntry; }
#endif

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
  ::grpc::Status SubmitPrewarm(::grpc::ServerContext* context,
                              const ::fluxcache::proto::SubmitPrewarmRequest* request,
                              ::fluxcache::proto::SubmitPrewarmResponse* response) override;

  ::grpc::Status Mkdir(::grpc::ServerContext* context,
                       const ::fluxcache::proto::MkdirRequest* request,
                       ::fluxcache::proto::MkdirResponse* response) override;
  ::grpc::Status Rmdir(::grpc::ServerContext* context,
                       const ::fluxcache::proto::RmdirRequest* request,
                       ::fluxcache::proto::RmdirResponse* response) override;
  ::grpc::Status ListDir(::grpc::ServerContext* context,
                         const ::fluxcache::proto::ListDirRequest* request,
                         ::fluxcache::proto::ListDirResponse* response) override;
  ::grpc::Status Rename(::grpc::ServerContext* context,
                        const ::fluxcache::proto::RenameRequest* request,
                        ::fluxcache::proto::RenameResponse* response) override;
  ::grpc::Status Stat(::grpc::ServerContext* context,
                      const ::fluxcache::proto::StatRequest* request,
                      ::fluxcache::proto::StatResponse* response) override;

  void CheckWorkerHealthAndUpdateRing(int64_t now_ms,
                                      int64_t heartbeat_timeout_ms,
                                      int64_t suspect_grace_ms);
  void SetWorkerHealthPolicy(int64_t heartbeat_timeout_ms,
                             int64_t suspect_grace_ms);

  void RunHeartbeatToAllWorkers();
  void UpdateActiveWorkersGauge();
  void CallWorkerHeartbeat(WorkerId worker_id, const std::string& host,
                          uint16_t port);

  void AddPendingOrphan(InodeId inode_id);
  void RemovePendingOrphans(const std::vector<uint64_t>& audit_inode_ids);

  void BindMountTableStore(InodeStore* store);
  void RecoverMountTable();

  MountTable* mount_table_ptr() { return &mount_table_; }
  WorkerManager* worker_manager_ptr() { return &worker_manager_; }
  HashRingManager* hash_ring_manager_ptr() { return &hash_ring_manager_; }

  Status ApplyReplicatedWorkerRegistration(WorkerId worker_id,
                                           const std::string& host,
                                           uint16_t port,
                                           int64_t last_heartbeat_ms,
                                           uint64_t next_worker_id);
  Status ApplyReplicatedWorkerState(WorkerId worker_id, WorkerState state,
                                    int64_t last_heartbeat_ms,
                                    int64_t suspect_since_ms);
  void BuildWorkerTopologySnapshot(proto::WorkerTopologySnapshot* snapshot) const;
  bool RestoreWorkerTopologySnapshot(
      const proto::WorkerTopologySnapshot& snapshot);

#ifdef FLUXCACHE_ENABLE_RAFT
  void SetRaftNode(RaftNode* node) { raft_node_ = node; }
#endif

 private:
  static ::grpc::Status ToGrpcStatus(const Status& s);

#ifdef FLUXCACHE_ENABLE_RAFT
  bool IsRaftEnabled() const { return raft_node_ != nullptr; }
  bool IsLeader() const;
  ::grpc::Status NotLeaderError() const;
  // Replicate a JournalEntry through Raft. Returns the result buffer.
  nuraft::ptr<nuraft::buffer> ReplicateEntry(
      const proto::JournalEntry& entry);
#endif

  InodeTree* inode_tree_;
  MetricsRegistry* metrics_;
  PathResolver path_resolver_;
  std::atomic<uint64_t> next_worker_id_{1};
  WorkerManager worker_manager_;
  HashRingManager hash_ring_manager_;
  MountTable mount_table_;
  std::unique_ptr<PrewarmQueue> prewarm_queue_;
  mutable std::mutex topology_mu_;
  std::atomic<int64_t> worker_heartbeat_timeout_ms_{15000};
  std::atomic<int64_t> worker_suspect_grace_ms_{15000};
  std::atomic<int64_t> topology_recovered_at_ms_{0};

  mutable std::mutex orphan_mu_;
  std::set<uint64_t> pending_orphan_inodes_;

#ifdef FLUXCACHE_ENABLE_RAFT
  RaftNode* raft_node_ = nullptr;
#endif
};

}  // namespace fluxcache
