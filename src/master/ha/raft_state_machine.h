#pragma once

#ifdef FLUXCACHE_ENABLE_RAFT

#include "libnuraft/nuraft.hxx"
#include <atomic>
#include <map>
#include <mutex>
#include <string>

namespace fluxcache {

class InodeTree;
class MountTable;
class MasterServiceImpl;

class RaftStateMachine : public nuraft::state_machine {
 public:
  RaftStateMachine(InodeTree* inode_tree, MountTable* mount_table,
                   const std::string& snapshot_path,
                   MasterServiceImpl* master_service = nullptr);
  ~RaftStateMachine() override = default;

  nuraft::ptr<nuraft::buffer> commit(const nuraft::ulong log_idx,
                                     nuraft::buffer& data) override;

  void commit_config(const nuraft::ulong log_idx,
                     nuraft::ptr<nuraft::cluster_config>& new_conf) override;

  bool apply_snapshot(nuraft::snapshot& s) override;

  nuraft::ptr<nuraft::snapshot> last_snapshot() override;

  nuraft::ulong last_commit_index() override;

  void create_snapshot(nuraft::snapshot& s,
                       nuraft::async_result<bool>::handler_type& when_done) override;

  int read_logical_snp_obj(nuraft::snapshot& s, void*& user_snp_ctx,
                           nuraft::ulong obj_id,
                           nuraft::ptr<nuraft::buffer>& data_out,
                           bool& is_last_obj) override;

  void save_logical_snp_obj(nuraft::snapshot& s, nuraft::ulong& obj_id,
                            nuraft::buffer& data, bool is_first_obj,
                            bool is_last_obj) override;

 private:
  struct SnapshotCtx {
    nuraft::ptr<nuraft::snapshot> snapshot_;
    std::string path_;
    std::vector<std::string> files_;
    size_t expected_files_ = 0;
  };

  std::shared_ptr<SnapshotCtx> CreateSnapshotInternal(
      nuraft::ptr<nuraft::snapshot> ss);

  InodeTree* inode_tree_;
  MountTable* mount_table_;
  MasterServiceImpl* master_service_;
  std::string snapshot_base_path_;
  std::atomic<uint64_t> last_committed_idx_{0};

  std::mutex snapshots_lock_;
  std::map<uint64_t, std::shared_ptr<SnapshotCtx>> snapshots_;
  std::map<uint64_t, std::shared_ptr<SnapshotCtx>> pending_snapshots_;
};

}  // namespace fluxcache

#endif  // FLUXCACHE_ENABLE_RAFT
