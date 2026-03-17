#ifdef FLUXCACHE_ENABLE_RAFT

#include "master/master_service_impl.h"
#include "master/ha/raft_node.h"
#include "master/ha/raft_state_machine.h"
#include "master/ha/raft_state_manager.h"
#include <iostream>

namespace fluxcache {

RaftNode::RaftNode(int server_id, const std::string& endpoint,
                   InodeTree* inode_tree, MountTable* mount_table,
                   const std::string& snapshot_path,
                   const std::string& storage_path,
                   MasterServiceImpl* master_service)
    : server_id_(server_id),
      endpoint_(endpoint),
      snapshot_path_(snapshot_path),
      storage_path_(storage_path) {
  state_machine_ = nuraft::cs_new<RaftStateMachine>(
      inode_tree, mount_table, snapshot_path, master_service);
  state_manager_ =
      nuraft::cs_new<RaftStateManager>(server_id, endpoint, storage_path);
  launcher_ = std::make_unique<nuraft::raft_launcher>();
}

RaftNode::~RaftNode() {
  Shutdown();
}

bool RaftNode::Start(int raft_port,
                     const std::vector<RaftPeerConfig>& peers,
                     int heartbeat_interval_ms,
                     int election_timeout_lower_ms,
                     int election_timeout_upper_ms,
                     int snapshot_distance,
                     int reserved_log_items) {
  nuraft::raft_params params;
  params.heart_beat_interval_ = heartbeat_interval_ms;
  params.election_timeout_lower_bound_ = election_timeout_lower_ms;
  params.election_timeout_upper_bound_ = election_timeout_upper_ms;
  params.snapshot_distance_ = snapshot_distance;
  params.reserved_log_items_ = reserved_log_items;
  params.return_method_ = nuraft::raft_params::blocking;

  nuraft::asio_service::options asio_opts;
  asio_opts.thread_pool_size_ = 4;

  state_manager_->ConfigureCluster(peers);
  if (!state_manager_->IsHealthy()) {
    std::cerr << "RaftNode: persistent state is unhealthy\n";
    return false;
  }

  auto raft_instance = launcher_->init(
      state_machine_, state_manager_, nullptr, raft_port, asio_opts, params);

  if (!raft_instance) {
    std::cerr << "RaftNode: failed to initialize Raft on port " << raft_port
              << "\n";
    return false;
  }

  return true;
}

void RaftNode::Shutdown() {
  if (launcher_) {
    launcher_->shutdown(5);
  }
}

bool RaftNode::IsLeader() const {
  if (!state_manager_ || !state_manager_->IsHealthy()) return false;
  auto srv = launcher_->get_raft_server();
  if (!srv) return false;
  return srv->is_leader();
}

int RaftNode::GetLeaderId() const {
  auto srv = launcher_->get_raft_server();
  if (!srv) return -1;
  return srv->get_leader();
}

std::string RaftNode::GetLeaderEndpoint() const {
  auto srv = launcher_->get_raft_server();
  if (!srv) return "";
  int leader_id = srv->get_leader();
  if (leader_id < 0) return "";
  auto config = srv->get_config();
  if (!config) return "";
  for (auto& s : config->get_servers()) {
    if (s->get_id() == leader_id) {
      return s->get_endpoint();
    }
  }
  return "";
}

nuraft::ptr<nuraft::buffer> RaftNode::Replicate(
    const std::string& serialized_entry) {
  if (!state_manager_ || !state_manager_->IsHealthy()) return nullptr;
  auto srv = launcher_->get_raft_server();
  if (!srv) return nullptr;

  nuraft::ptr<nuraft::buffer> buf =
      nuraft::buffer::alloc(serialized_entry.size());
  buf->put_raw(reinterpret_cast<const nuraft::byte*>(serialized_entry.data()),
               serialized_entry.size());
  buf->pos(0);

  auto ret = srv->append_entries({buf});
  if (!ret->get_accepted()) {
    return nullptr;
  }

  auto result = ret->get();
  if (!result) return nullptr;
  if (!state_manager_->IsHealthy()) return nullptr;
  return result;
}

nuraft::ptr<nuraft::raft_server> RaftNode::GetRaftServer() const {
  return launcher_->get_raft_server();
}

RaftStateMachine* RaftNode::GetStateMachine() const {
  return state_machine_.get();
}

RaftStateManager* RaftNode::GetStateManager() const {
  return state_manager_.get();
}

}  // namespace fluxcache

#endif  // FLUXCACHE_ENABLE_RAFT
