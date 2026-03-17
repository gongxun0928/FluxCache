#pragma once

#ifdef FLUXCACHE_ENABLE_RAFT

#include "master/ha/raft_state_manager.h"
#include <memory>
#include <string>
#include <vector>

namespace fluxcache {

class InodeTree;
class MountTable;
class MasterServiceImpl;
class RaftStateMachine;
class RaftStateManager;

class RaftNode {
 public:
  RaftNode(int server_id, const std::string& endpoint,
           InodeTree* inode_tree, MountTable* mount_table,
           const std::string& snapshot_path,
           const std::string& storage_path,
           MasterServiceImpl* master_service = nullptr);
  ~RaftNode();

  RaftNode(const RaftNode&) = delete;
  RaftNode& operator=(const RaftNode&) = delete;

  bool Start(int raft_port, const std::vector<RaftPeerConfig>& peers,
             int heartbeat_interval_ms = 100,
             int election_timeout_lower_ms = 200,
             int election_timeout_upper_ms = 400,
             int snapshot_distance = 10000,
             int reserved_log_items = 5000);

  void Shutdown();

  bool IsLeader() const;
  int GetLeaderId() const;
  std::string GetLeaderEndpoint() const;

  // Submit a serialized JournalEntry to Raft. Blocks until committed or fails.
  // Returns the result buffer from state_machine::commit, or nullptr on failure.
  nuraft::ptr<nuraft::buffer> Replicate(const std::string& serialized_entry);

  nuraft::ptr<nuraft::raft_server> GetRaftServer() const;
  RaftStateMachine* GetStateMachine() const;
  RaftStateManager* GetStateManager() const;

 private:
  int server_id_;
  std::string endpoint_;
  std::string snapshot_path_;
  std::string storage_path_;
  nuraft::ptr<RaftStateMachine> state_machine_;
  nuraft::ptr<RaftStateManager> state_manager_;
  std::unique_ptr<nuraft::raft_launcher> launcher_;
};

}  // namespace fluxcache

#endif  // FLUXCACHE_ENABLE_RAFT
