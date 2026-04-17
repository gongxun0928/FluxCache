#pragma once

#include "common/status.h"
#include "master/inode_store.h"
#include "common/types.h"
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fluxcache {

// In-memory directory skeleton for path resolution (design/metadata-design.md §2.1.2).
struct DirNode {
  InodeId id = 0;
  InodeId parent_id = 0;
  std::string name;
  std::unordered_map<std::string, InodeId> children;
};

// InodeTree: path ↔ InodeId mapping with RocksDB persistence.
// Phase 1: single shared_mutex for all operations.
// Root inode_id is always 1.
class InodeTree {
 public:
  explicit InodeTree(const std::string& db_path);
  ~InodeTree();

  InodeTree(const InodeTree&) = delete;
  InodeTree& operator=(const InodeTree&) = delete;

  // Opens DB and either initializes root (first run) or recovers from RocksDB.
  // Returns false on failure.
  bool InitOrRecover();

  // Returns true if InitOrRecover succeeded.
  bool is_ready() const { return ready_; }

  // Path lookup: returns inode_id if path exists, nullopt otherwise.
  // Path must be absolute (start with /).
  std::optional<InodeId> LookupPath(const std::string& path) const;

  // Create file at path. Parent directory must exist.
  // Returns inode_id on success, nullopt on failure (e.g. parent missing, already exists).
  std::optional<InodeId> CreateFile(const std::string& path);

  // Create file with UFS metadata (size, block_size, mtime). Used by PathResolver SyncFromUfs.
  std::optional<InodeId> CreateFile(const std::string& path, uint64_t size,
                                    uint64_t block_size, int64_t mtime_ms);

  // Create directory at path. Parent must exist.
  std::optional<InodeId> CreateDirectory(const std::string& path);

  // Delete inode immediately (removes from inodes/edges, persists).
  // For directories, must be empty.
  // Returns true if deleted.
  bool DeleteInode(InodeId id);

  // List direct children of directory. Returns (name, inode_id) pairs.
  std::vector<std::pair<std::string, InodeId>> ListDirectory(InodeId dir_id);

  // Get inode entry for path (for FileInfo).
  std::optional<InodeEntry> GetInode(InodeId id);

  // Rename inode: move src_path to dst_path.
  // Supports cross-directory moves and overwriting existing dst (if non-dir or empty dir).
  // Returns true on success.
  bool RenameInode(const std::string& src_path, const std::string& dst_path);

  // Update inode size and increment file_version. Used by CompleteFile.
  // Returns false if inode not found or is directory.
  bool UpdateInodeSizeAndIncrementVersion(InodeId id, uint64_t size);

  // Returns true if any inode exists whose logical path equals prefix or is
  // under prefix (e.g. /data or /data/file). Used for Unmount safety check.
  bool HasInodesUnderPath(const std::string& prefix) const;

  // Expose InodeStore for MountTable persistence binding.
  InodeStore* store() { return store_.get(); }

  // Pre-allocate an InodeId for Raft log entries (leader side only).
  // Returns the allocated id and the updated next_id counter.
  struct AllocResult { InodeId id; InodeId next_id; };
  AllocResult AllocateInodeId();

  // Deterministic apply methods called by RaftStateMachine::commit().
  // These use pre-allocated IDs from the Raft log entry for determinism.
  Status ApplyCreateFile(InodeId id, const std::string& path, InodeId parent_id,
                         uint64_t size, uint64_t block_size,
                         int64_t creation_time_ms, int64_t mtime_ms,
                         InodeId next_id);
  Status ApplyCreateDirectory(InodeId id, const std::string& path,
                              InodeId parent_id, int64_t creation_time_ms,
                              int64_t modification_time_ms, InodeId next_id);
  Status ApplyDeleteInode(InodeId id);
  Status ApplyUpdateSizeAndIncrementVersion(InodeId id, uint64_t size,
                                            uint64_t file_version);

  // Resolve path components to find parent_id for a given path.
  // Used by leader before creating Raft log entries.
  std::optional<InodeId> FindParentId(const std::string& path) const;

  // Create a consistent RocksDB checkpoint of current metadata state.
  bool CreateCheckpoint(const std::string& path);

  // Replace current metadata DB contents with a checkpoint and recover memory state.
  bool RestoreFromCheckpoint(const std::string& checkpoint_path);

 private:
  std::string db_path_;
  std::unique_ptr<InodeStore> store_;
  bool ready_ = false;

  // DirNode id -> DirNode (for directories only; files are in store only)
  std::unordered_map<InodeId, DirNode> dirs_;
  InodeId next_id_ = 2;  // 1 is root

  mutable std::shared_mutex mu_;

  bool InitRoot();
  bool Recover();
  std::vector<std::string> SplitPath(const std::string& path) const;
  DirNode* GetDirNode(InodeId id);
  const DirNode* GetDirNode(InodeId id) const;
};

}  // namespace fluxcache
