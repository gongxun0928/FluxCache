#pragma once

#include "common/status.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace fluxcache {

class InodeStore;

// MountTable maps logical paths to UFS URIs. Resolve uses longest-prefix match
// for nested mounts. Thread-safe.
// When an InodeStore is bound, Mount/Unmount are persisted to RocksDB.
class MountTable {
 public:
  MountTable() = default;

  // Bind to InodeStore for persistence. Must be called before RecoverFromStore.
  void BindStore(InodeStore* store);

  // Recover mount entries from RocksDB. Call after BindStore + InodeStore::Open.
  void RecoverFromStore();

  // Mount logical path to ufs_uri. Returns InvalidArgument if path already
  // mounted. Persists to RocksDB if store is bound.
  Status Mount(const std::string& path, const std::string& ufs_uri);

  // Unmount the given path. Returns NotFound if path is not mounted.
  // Persists to RocksDB if store is bound.
  Status Unmount(const std::string& path);

  // Resolve logical_path to (ufs_uri, ufs_path) using longest-prefix match.
  // Returns NotFound if no mount point matches.
  Status Resolve(const std::string& logical_path, std::string* ufs_uri,
                 std::string* ufs_path) const;

  // List all mount paths in lexicographic order.
  std::vector<std::string> ListMounts() const;

  // Deterministic apply methods called by RaftStateMachine::commit().
  // These update both in-memory state and RocksDB (via store_).
  Status ApplyMount(const std::string& path, const std::string& ufs_uri);
  Status ApplyUnmount(const std::string& path);

 private:
  static std::string NormalizePath(const std::string& path);
  static bool IsPrefixOf(const std::string& mount_path,
                         const std::string& logical_path);

  InodeStore* store_ = nullptr;
  mutable std::mutex mu_;
  std::map<std::string, std::string> mounts_;
};

}  // namespace fluxcache
