#pragma once

#include "common/status.h"
#include "master/inode_tree.h"
#include "master/mount_table.h"
#include "ufs/ufs.h"

#include <memory>
#include <string>
#include <vector>

namespace fluxcache {

// PathResolver coordinates InodeTree, MountTable, and UFS to resolve paths and
// sync inodes from UFS on demand. Used by GetFileInfo, ListDirectory, etc.
class PathResolver {
 public:
  PathResolver(InodeTree* tree, MountTable* mount_table);

  // Syncs the given logical path from UFS into InodeTree. Ensures the path and
  // all ancestors exist. Returns OK on success.
  Status SyncFromUfs(const std::string& logical_path);

  // Resolves path, syncs if needed, and returns inode_id. Returns nullopt if
  // path does not exist in UFS or sync fails.
  std::optional<InodeId> ResolveOrSync(const std::string& logical_path);

  // Lists directory at path after syncing from UFS. Returns (name, inode_id).
  std::vector<std::pair<std::string, InodeId>> ListDirectory(
      const std::string& logical_path);

 private:
  static bool ParseUfsUri(const std::string& ufs_uri, std::string* scheme,
                          std::string* authority);
  static std::string Dirname(const std::string& path);
  static std::string JoinPath(const std::string& a, const std::string& b);

  InodeTree* tree_;
  MountTable* mount_table_;
};

}  // namespace fluxcache
