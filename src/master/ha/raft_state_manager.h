#pragma once

#ifdef FLUXCACHE_ENABLE_RAFT

#include "libnuraft/nuraft.hxx"
#include <string>
#include <vector>

namespace fluxcache {

struct RaftPeerConfig {
  int id;
  std::string endpoint;
};

class RaftStateManager : public nuraft::state_mgr {
 public:
  RaftStateManager(int srv_id, const std::string& endpoint,
                   const std::string& storage_dir);
  ~RaftStateManager() override = default;

  nuraft::ptr<nuraft::cluster_config> load_config() override;
  void save_config(const nuraft::cluster_config& config) override;
  void save_state(const nuraft::srv_state& state) override;
  nuraft::ptr<nuraft::srv_state> read_state() override;
  nuraft::ptr<nuraft::log_store> load_log_store() override;
  int32_t server_id() override;
  void system_exit(const int exit_code) override;

  void ConfigureCluster(const std::vector<RaftPeerConfig>& peers);
  bool IsHealthy() const;
  void FailNextLogWriteForTest(uint32_t count = 1);

 private:
  int my_id_;
  std::string my_endpoint_;
  std::string storage_dir_;
  nuraft::ptr<nuraft::log_store> log_store_;
  nuraft::ptr<nuraft::srv_config> my_srv_config_;
  nuraft::ptr<nuraft::cluster_config> saved_config_;
  nuraft::ptr<nuraft::srv_state> saved_state_;
  bool loaded_config_from_disk_ = false;
  bool healthy_ = true;
};

}  // namespace fluxcache

#endif  // FLUXCACHE_ENABLE_RAFT
