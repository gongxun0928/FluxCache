#include "master/inode_tree.h"
#include "master/master_server.h"
#include "master/master_service_impl.h"
#include "common/metrics/http_metrics_server.h"
#include "common/metrics/metrics_registry.h"
#ifdef FLUXCACHE_ENABLE_RAFT
#include "master/ha/raft_node.h"
#endif
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <iostream>

namespace fluxcache {

namespace {

constexpr auto kWorkerMaintenanceInterval = std::chrono::seconds(10);
constexpr int64_t kWorkerHeartbeatTimeoutMs = 15000;
constexpr int64_t kWorkerSuspectGraceMs = 15000;

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

MasterServer::MasterServer(const MasterConfig& config) : config_(config) {
  inode_tree_ = std::make_unique<InodeTree>(config_.db_path);
  if (config_.metrics_port > 0) {
    metrics_registry_ = std::make_unique<MetricsRegistry>();
    service_impl_ = std::make_unique<MasterServiceImpl>(inode_tree_.get(),
                                                          metrics_registry_.get());
    http_metrics_server_ =
        std::make_unique<HttpMetricsServer>(config_.metrics_port,
                                            metrics_registry_.get());
  } else {
    service_impl_ = std::make_unique<MasterServiceImpl>(inode_tree_.get(),
                                                          nullptr);
  }
}

MasterServer::~MasterServer() {
  Shutdown();
}

bool MasterServer::Start() {
  if (!inode_tree_->InitOrRecover()) {
    return false;
  }
  service_impl_->BindMountTableStore(inode_tree_->store());
  service_impl_->RecoverMountTable();
  service_impl_->SetWorkerHealthPolicy(kWorkerHeartbeatTimeoutMs,
                                       kWorkerSuspectGraceMs);

#ifdef FLUXCACHE_ENABLE_RAFT
  if (config_.raft.enabled) {
    std::string endpoint =
        config_.host + ":" + std::to_string(config_.raft.raft_port);
    raft_node_ = std::make_unique<RaftNode>(
        config_.raft.server_id, endpoint, inode_tree_.get(),
        service_impl_->mount_table_ptr(), config_.raft.snapshot_path,
        config_.raft.snapshot_path + "/raft_state", service_impl_.get());

    std::vector<RaftPeerConfig> peers;
    for (const auto& p : config_.raft.peers) {
      peers.push_back({p.id, p.endpoint});
    }

    if (!raft_node_->Start(
            config_.raft.raft_port, peers,
            config_.raft.heartbeat_interval_ms,
            config_.raft.election_timeout_lower_ms,
            config_.raft.election_timeout_upper_ms,
            config_.raft.snapshot_distance,
            config_.raft.reserved_log_items)) {
      std::cerr << "Failed to start Raft node\n";
      return false;
    }
    service_impl_->SetRaftNode(raft_node_.get());
    std::cerr << "Raft node started (server_id=" << config_.raft.server_id
              << ", port=" << config_.raft.raft_port << ")\n";
  }
#endif

  if (http_metrics_server_ && !http_metrics_server_->Start()) {
    return false;
  }
  std::string addr = config_.host + ":" + std::to_string(config_.port);
  ::grpc::ServerBuilder builder;
  builder.AddListeningPort(addr, ::grpc::InsecureServerCredentials());
  builder.RegisterService(service_impl_.get());
  server_ = builder.BuildAndStart();
  if (!server_) return false;

  heartbeat_stop_.store(false);
  if (service_impl_) service_impl_->UpdateActiveWorkersGauge();
  heartbeat_thread_ = std::thread([this]() {
    while (!heartbeat_stop_.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(kWorkerMaintenanceInterval);
      if (heartbeat_stop_.load(std::memory_order_relaxed)) break;
      if (service_impl_) {
        service_impl_->RunHeartbeatToAllWorkers();
        service_impl_->CheckWorkerHealthAndUpdateRing(
            NowMs(), kWorkerHeartbeatTimeoutMs, kWorkerSuspectGraceMs);
      }
    }
  });
  return true;
}

void MasterServer::Shutdown() {
  heartbeat_stop_.store(true);
  if (heartbeat_thread_.joinable()) {
    heartbeat_thread_.join();
  }
#ifdef FLUXCACHE_ENABLE_RAFT
  if (raft_node_) {
    raft_node_->Shutdown();
    raft_node_.reset();
  }
#endif
  if (http_metrics_server_) {
    http_metrics_server_->Shutdown();
  }
  if (server_) {
    server_->Shutdown();
    server_->Wait();
    server_.reset();
  }
}

}  // namespace fluxcache
