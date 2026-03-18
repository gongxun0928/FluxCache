#ifdef FLUXCACHE_ENABLE_RAFT

#include "master/ha/raft_result.h"
#include "master/ha/raft_state_machine.h"
#include "master/inode_tree.h"
#include "master/master_service_impl.h"
#include "master/mount_table.h"
#include "master.pb.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fluxcache {

namespace {

namespace fs = std::filesystem;

fs::path SnapshotDirFor(const std::string& base_path, uint64_t idx) {
  return fs::path(base_path) / ("snap_" + std::to_string(idx));
}

fs::path SnapshotTempDirFor(const std::string& base_path, uint64_t idx) {
  return fs::path(base_path) / ("snap_" + std::to_string(idx) + ".tmp");
}

fs::path IncomingSnapshotDirFor(const std::string& base_path, uint64_t idx) {
  return fs::path(base_path) / ("incoming_snap_" + std::to_string(idx));
}

fs::path IncomingSnapshotTempDirFor(const std::string& base_path, uint64_t idx) {
  return fs::path(base_path) / ("incoming_snap_" + std::to_string(idx) + ".tmp");
}

fs::path WorkerTopologySnapshotPath(const fs::path& snapshot_dir) {
  return snapshot_dir / "worker_topology.pb";
}

fs::path SnapshotMetaPath(const fs::path& snapshot_dir) {
  return snapshot_dir / "snapshot.meta";
}

std::vector<std::string> ListSnapshotFiles(const std::string& base_path) {
  std::vector<std::string> files;
  if (!fs::exists(base_path)) return files;
  for (const auto& entry : fs::recursive_directory_iterator(base_path)) {
    if (!entry.is_regular_file()) continue;
    files.push_back(fs::relative(entry.path(), base_path).string());
  }
  std::sort(files.begin(), files.end());
  return files;
}

bool ReadFileBytes(const fs::path& path, std::string* out) {
  if (!out) return false;
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return false;
  in.seekg(0, std::ios::end);
  std::streamsize size = in.tellg();
  in.seekg(0, std::ios::beg);
  if (size < 0) return false;
  out->resize(static_cast<size_t>(size));
  if (size > 0) {
    in.read(out->data(), size);
    if (!in.good() && !in.eof()) return false;
  }
  return true;
}

bool WriteFileBytes(const fs::path& path, const void* data, size_t len) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;
  out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
  return out.good();
}

bool ParseSnapshotIndex(const std::string& name, const std::string& prefix,
                        uint64_t* index_out) {
  if (!index_out) return false;
  if (name.rfind(prefix, 0) != 0 || name.size() <= prefix.size()) return false;
  try {
    *index_out = std::stoull(name.substr(prefix.size()));
    return true;
  } catch (...) {
    return false;
  }
}

bool SerializeSnapshotMeta(nuraft::ptr<nuraft::snapshot> snapshot,
                           std::string* out) {
  if (!snapshot || !out) return false;
  auto buf = snapshot->serialize();
  if (!buf) return false;
  out->assign(reinterpret_cast<const char*>(buf->data()), buf->size());
  return true;
}

nuraft::ptr<nuraft::snapshot> DeserializeSnapshotMeta(const std::string& bytes) {
  auto buf = nuraft::buffer::alloc(bytes.size());
  if (bytes.size() > 0) {
    buf->put_raw(reinterpret_cast<const nuraft::byte*>(bytes.data()),
                 bytes.size());
  }
  buf->pos(0);
  return nuraft::snapshot::deserialize(*buf);
}

}  // namespace

RaftStateMachine::RaftStateMachine(InodeTree* inode_tree, MountTable* mount_table,
                                   const std::string& snapshot_path,
                                   MasterServiceImpl* master_service)
    : inode_tree_(inode_tree),
      mount_table_(mount_table),
      master_service_(master_service),
      snapshot_base_path_(snapshot_path) {
  std::filesystem::create_directories(snapshot_base_path_);
  std::vector<std::pair<uint64_t, std::shared_ptr<SnapshotCtx>>> recovered;
  for (const auto& entry : fs::directory_iterator(snapshot_base_path_)) {
    if (!entry.is_directory()) continue;
    const std::string name = entry.path().filename().string();
    uint64_t log_idx = 0;
    if (!ParseSnapshotIndex(name, "snap_", &log_idx)) {
      continue;
    }

    if (master_service_) {
      proto::WorkerTopologySnapshot topology;
      std::string bytes;
      if (!ReadFileBytes(WorkerTopologySnapshotPath(entry.path()), &bytes) ||
          !topology.ParseFromString(bytes)) {
        continue;
      }
    }

    auto ctx = std::make_shared<SnapshotCtx>();
    ctx->path_ = entry.path().string();
    ctx->files_ = ListSnapshotFiles(ctx->path_);
    if (ctx->files_.empty()) continue;

    std::string meta_bytes;
    if (ReadFileBytes(SnapshotMetaPath(entry.path()), &meta_bytes) &&
        !meta_bytes.empty()) {
      ctx->snapshot_ = DeserializeSnapshotMeta(meta_bytes);
      if (!ctx->snapshot_ ||
          ctx->snapshot_->get_last_log_idx() != static_cast<nuraft::ulong>(log_idx)) {
        continue;
      }
    } else {
      auto cfg = nuraft::cs_new<nuraft::cluster_config>();
      ctx->snapshot_ = nuraft::cs_new<nuraft::snapshot>(
          nuraft::ulong(log_idx), nuraft::ulong(0), cfg);
    }
    ctx->expected_files_ = ctx->files_.size();
    ctx->snapshot_->set_size(ctx->files_.size());
    recovered.emplace_back(log_idx, ctx);
  }

  std::sort(recovered.begin(), recovered.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  if (recovered.size() > 3) {
    recovered.erase(recovered.begin(), recovered.end() - 3);
  }
  for (const auto& [log_idx, ctx] : recovered) {
    snapshots_[log_idx] = ctx;
  }
}

nuraft::ptr<nuraft::buffer> RaftStateMachine::commit(const nuraft::ulong log_idx,
                                                     nuraft::buffer& data) {
  proto::JournalEntry entry;
  if (!entry.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
    std::cerr << "RaftStateMachine: failed to parse JournalEntry at log_idx="
              << log_idx << "\n";
    last_committed_idx_ = log_idx;
    return nullptr;
  }

  nuraft::ptr<nuraft::buffer> result = nullptr;

  switch (entry.op_case()) {
    case proto::JournalEntry::kCreateFile: {
      const auto& op = entry.create_file();
      Status status = inode_tree_->ApplyCreateFile(
          op.inode_id(), op.path(), op.parent_id(), op.size(), op.block_size(),
          op.creation_time_ms(), op.mtime_ms(), op.next_id());
      result = SerializeRaftApplyResult(status.code(), op.inode_id());
      break;
    }
    case proto::JournalEntry::kCompleteFile: {
      const auto& op = entry.complete_file();
      Status status = inode_tree_->ApplyUpdateSizeAndIncrementVersion(
          op.inode_id(), op.size(), op.file_version());
      result = SerializeRaftApplyResult(status.code());
      break;
    }
    case proto::JournalEntry::kDeleteFile: {
      const auto& op = entry.delete_file();
      Status status = inode_tree_->ApplyDeleteInode(op.inode_id());
      result = SerializeRaftApplyResult(status.code());
      break;
    }
    case proto::JournalEntry::kMountOp: {
      const auto& op = entry.mount_op();
      Status status = mount_table_->ApplyMount(op.path(), op.ufs_uri());
      result = SerializeRaftApplyResult(status.code());
      break;
    }
    case proto::JournalEntry::kUnmountOp: {
      const auto& op = entry.unmount_op();
      Status status = mount_table_->ApplyUnmount(op.path());
      result = SerializeRaftApplyResult(status.code());
      break;
    }
    case proto::JournalEntry::kCreateDirectory: {
      const auto& op = entry.create_directory();
      Status status = inode_tree_->ApplyCreateDirectory(
          op.inode_id(), op.path(), op.parent_id(), op.creation_time_ms(),
          op.modification_time_ms(), op.next_id());
      result = SerializeRaftApplyResult(status.code(), op.inode_id());
      break;
    }
    case proto::JournalEntry::kUpsertWorker: {
      const auto& op = entry.upsert_worker();
      if (!master_service_) {
        result = SerializeRaftApplyResult(StatusCode::kInvalidArgument);
        break;
      }
      Status status = master_service_->ApplyReplicatedWorkerRegistration(
          op.worker_id(), op.host(), static_cast<uint16_t>(op.port()),
          op.last_heartbeat_ms(), op.next_worker_id());
      result = SerializeRaftApplyResult(status.code(), op.worker_id());
      break;
    }
    case proto::JournalEntry::kUpdateWorkerState: {
      const auto& op = entry.update_worker_state();
      if (!master_service_) {
        result = SerializeRaftApplyResult(StatusCode::kInvalidArgument);
        break;
      }
      Status status = master_service_->ApplyReplicatedWorkerState(
          op.worker_id(), static_cast<WorkerState>(op.state()),
          op.last_heartbeat_ms(), op.suspect_since_ms());
      result = SerializeRaftApplyResult(status.code());
      break;
    }
    default:
      break;
  }

  last_committed_idx_ = log_idx;
  return result;
}

void RaftStateMachine::commit_config(
    const nuraft::ulong log_idx,
    nuraft::ptr<nuraft::cluster_config>& /*new_conf*/) {
  last_committed_idx_ = log_idx;
}

bool RaftStateMachine::apply_snapshot(nuraft::snapshot& s) {
  std::shared_ptr<SnapshotCtx> ctx;
  {
    std::lock_guard<std::mutex> ll(snapshots_lock_);
    auto it = snapshots_.find(s.get_last_log_idx());
    if (it == snapshots_.end()) return false;
    ctx = it->second;
  }
  if (!ctx) return false;
  if (!inode_tree_->RestoreFromCheckpoint(ctx->path_)) return false;
  mount_table_->RecoverFromStore();
  if (master_service_) {
    proto::WorkerTopologySnapshot topology;
    std::string bytes;
    if (!ReadFileBytes(WorkerTopologySnapshotPath(ctx->path_), &bytes)) return false;
    if (!topology.ParseFromString(bytes)) return false;
    if (!master_service_->RestoreWorkerTopologySnapshot(topology)) return false;
  }
  last_committed_idx_ = std::max<uint64_t>(last_committed_idx_, s.get_last_log_idx());
  return true;
}

nuraft::ptr<nuraft::snapshot> RaftStateMachine::last_snapshot() {
  std::lock_guard<std::mutex> ll(snapshots_lock_);
  auto it = snapshots_.rbegin();
  if (it == snapshots_.rend()) return nullptr;
  return it->second->snapshot_;
}

nuraft::ulong RaftStateMachine::last_commit_index() {
  return last_committed_idx_;
}

std::shared_ptr<RaftStateMachine::SnapshotCtx>
RaftStateMachine::CreateSnapshotInternal(nuraft::ptr<nuraft::snapshot> ss) {
  auto ctx = std::make_shared<SnapshotCtx>();
  ctx->snapshot_ = ss;
  ctx->path_ = SnapshotDirFor(snapshot_base_path_, ss->get_last_log_idx()).string();
  const fs::path tmp_path =
      SnapshotTempDirFor(snapshot_base_path_, ss->get_last_log_idx());
  const fs::path final_path = ctx->path_;
  std::error_code ec;
  fs::remove_all(tmp_path, ec);
  fs::create_directories(tmp_path.parent_path(), ec);
  if (ec || !inode_tree_->CreateCheckpoint(tmp_path.string())) {
    fs::remove_all(tmp_path, ec);
    return nullptr;
  }
  if (master_service_) {
    proto::WorkerTopologySnapshot topology;
    master_service_->BuildWorkerTopologySnapshot(&topology);
    std::string bytes;
    if (!topology.SerializeToString(&bytes) ||
        !WriteFileBytes(WorkerTopologySnapshotPath(tmp_path), bytes.data(), bytes.size())) {
      fs::remove_all(tmp_path, ec);
      return nullptr;
    }
  }
  size_t logical_file_count = ListSnapshotFiles(tmp_path.string()).size() + 1;
  ctx->snapshot_->set_size(logical_file_count);
  std::string meta_bytes;
  if (!SerializeSnapshotMeta(ctx->snapshot_, &meta_bytes) ||
      !WriteFileBytes(SnapshotMetaPath(tmp_path), meta_bytes.data(),
                      meta_bytes.size())) {
    fs::remove_all(tmp_path, ec);
    return nullptr;
  }
  ctx->files_ = ListSnapshotFiles(tmp_path.string());
  ctx->expected_files_ = ctx->files_.size();
  ctx->snapshot_->set_size(ctx->files_.size());

  fs::remove_all(final_path, ec);
  fs::rename(tmp_path, final_path, ec);
  if (ec) {
    fs::remove_all(tmp_path, ec);
    return nullptr;
  }

  std::lock_guard<std::mutex> ll(snapshots_lock_);
  snapshots_[ss->get_last_log_idx()] = ctx;

  // Keep last 3 snapshots only.
  while (snapshots_.size() > 3) {
    auto oldest = snapshots_.begin();
    auto& old_path = oldest->second->path_;
    std::error_code ec;
    std::filesystem::remove_all(old_path, ec);
    snapshots_.erase(oldest);
  }
  return ctx;
}

void RaftStateMachine::create_snapshot(
    nuraft::snapshot& s,
    nuraft::async_result<bool>::handler_type& when_done) {
  nuraft::ptr<nuraft::buffer> snp_buf = s.serialize();
  nuraft::ptr<nuraft::snapshot> ss = nuraft::snapshot::deserialize(*snp_buf);
  bool ret = (CreateSnapshotInternal(ss) != nullptr);

  nuraft::ptr<std::exception> except(nullptr);
  when_done(ret, except);
}

int RaftStateMachine::read_logical_snp_obj(nuraft::snapshot& s,
                                           void*& /*user_snp_ctx*/,
                                           nuraft::ulong obj_id,
                                           nuraft::ptr<nuraft::buffer>& data_out,
                                           bool& is_last_obj) {
  std::lock_guard<std::mutex> ll(snapshots_lock_);
  auto it = snapshots_.find(s.get_last_log_idx());
  if (it == snapshots_.end()) {
    data_out = nullptr;
    is_last_obj = true;
    return -1;
  }
  const auto& ctx = it->second;
  if (obj_id >= ctx->files_.size()) {
    data_out = nullptr;
    is_last_obj = true;
    return -1;
  }

  const std::string& rel_path = ctx->files_[obj_id];
  std::string file_bytes;
  if (!ReadFileBytes(fs::path(ctx->path_) / rel_path, &file_bytes)) {
    data_out = nullptr;
    is_last_obj = true;
    return -1;
  }

  data_out = nuraft::buffer::alloc(sizeof(uint32_t) + rel_path.size() +
                                   sizeof(uint32_t) + file_bytes.size());
  nuraft::buffer_serializer bs(data_out);
  bs.put_str(rel_path);
  bs.put_bytes(file_bytes.data(), file_bytes.size());
  is_last_obj = (obj_id + 1 == ctx->files_.size());
  return 0;
}

void RaftStateMachine::save_logical_snp_obj(nuraft::snapshot& s,
                                            nuraft::ulong& obj_id,
                                            nuraft::buffer& data,
                                            bool is_first_obj,
                                            bool is_last_obj) {
  std::shared_ptr<SnapshotCtx> ctx;
  if (is_first_obj) {
    nuraft::ptr<nuraft::buffer> snp_buf = s.serialize();
    nuraft::ptr<nuraft::snapshot> ss = nuraft::snapshot::deserialize(*snp_buf);
    ctx = std::make_shared<SnapshotCtx>();
    ctx->snapshot_ = ss;
    ctx->path_ =
        IncomingSnapshotDirFor(snapshot_base_path_, ss->get_last_log_idx()).string();
    ctx->expected_files_ = ss->size();
    const fs::path tmp_path =
        IncomingSnapshotTempDirFor(snapshot_base_path_, ss->get_last_log_idx());
    std::error_code ec;
    fs::remove_all(tmp_path, ec);
    fs::remove_all(ctx->path_, ec);
    fs::create_directories(tmp_path, ec);
    if (ec) return;
    ctx->path_ = tmp_path.string();
    std::lock_guard<std::mutex> ll(snapshots_lock_);
    pending_snapshots_[ss->get_last_log_idx()] = ctx;
  } else {
    std::lock_guard<std::mutex> ll(snapshots_lock_);
    auto it = pending_snapshots_.find(s.get_last_log_idx());
    if (it != pending_snapshots_.end()) {
      ctx = it->second;
    }
  }
  if (!ctx) return;

  nuraft::buffer_serializer bs(data);
  std::string rel_path = bs.get_str();
  size_t len = 0;
  void* raw = bs.get_bytes(len);
  if (WriteFileBytes(fs::path(ctx->path_) / rel_path, raw, len)) {
    ctx->files_.push_back(rel_path);
  }
  obj_id++;

  if (!is_last_obj) return;

  std::error_code ec;
  const fs::path final_path =
      IncomingSnapshotDirFor(snapshot_base_path_, s.get_last_log_idx());
  bool complete = ctx->files_.size() == ctx->expected_files_;
  if (complete) {
    fs::remove_all(final_path, ec);
    fs::rename(ctx->path_, final_path, ec);
    complete = !ec;
  }

  std::lock_guard<std::mutex> ll(snapshots_lock_);
  pending_snapshots_.erase(s.get_last_log_idx());
  if (!complete) {
    fs::remove_all(ctx->path_, ec);
    return;
  }
  ctx->path_ = final_path.string();
  snapshots_[s.get_last_log_idx()] = ctx;
}

}  // namespace fluxcache

#endif  // FLUXCACHE_ENABLE_RAFT
