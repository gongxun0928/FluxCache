#pragma once

#include "common/types.h"
#include <functional>
#include <optional>
#include <rocksdb/db.h>
#include <string>
#include <vector>

namespace fluxcache {

// Compact binary inode entry for RocksDB inodes CF (design/metadata-design.md §2.1.3).
// Phase 1 minimal: parent_id, size, is_directory, name.
struct InodeEntry {
  InodeId parent_id = 0;
  uint64_t size = 0;
  uint64_t block_size = 0;
  int64_t creation_time_ms = 0;
  int64_t modification_time_ms = 0;
  uint64_t file_version = 0;
  uint32_t mode = 0755;
  uint8_t flags = 0;  // bit0: is_directory, bit1: is_complete, bit2: is_deleting
  uint8_t owner_id = 0;
  uint8_t group_id = 0;
  uint8_t _padding = 0;
  std::string name;

  bool is_directory() const { return (flags & 1) != 0; }
  void set_directory(bool v) {
    if (v)
      flags |= 1;
    else
      flags &= ~1;
  }
};

// RocksDB-backed store for inodes and edges CFs.
// Key: inodes = InodeId (8B big-endian); edges = ParentId(8B) + ChildName.
// Value: inodes = InodeEntry binary; edges = ChildId(8B big-endian).
class InodeStore {
 public:
  InodeStore() = default;
  ~InodeStore() = default;
  InodeStore(const InodeStore&) = delete;
  InodeStore& operator=(const InodeStore&) = delete;

  // Opens or creates DB at path. Returns false on failure.
  bool Open(const std::string& path);

  void Close();

  bool is_open() const { return db_ != nullptr; }

  // Inodes CF
  bool PutInode(InodeId id, const InodeEntry& entry);
  std::optional<InodeEntry> GetInode(InodeId id);
  bool DeleteInode(InodeId id);

  // Edges CF: parent_id + child_name -> child_id
  bool PutEdge(InodeId parent_id, const std::string& child_name, InodeId child_id);
  std::optional<InodeId> GetEdge(InodeId parent_id, const std::string& child_name);
  bool DeleteEdge(InodeId parent_id, const std::string& child_name);

  // Iterate all edges for a parent (for recovery / ListDirectory from store)
  std::vector<std::pair<std::string, InodeId>> GetEdges(InodeId parent_id);

  // Iterate all inodes (for recovery). Skips next_id key.
  void IterateInodes(std::function<void(InodeId id, const InodeEntry& entry)> fn);

  // Iterate all edges (parent_id, child_name, child_id) for recovery.
  void IterateAllEdges(
      std::function<void(InodeId parent_id, const std::string& child_name, InodeId child_id)> fn);

  // Next InodeId counter (persisted)
  bool PutNextId(InodeId next);
  InodeId GetNextId();  // returns 1 if not found (first run)

  // Mounts CF: path -> ufs_uri. For MountTable persistence.
  bool PutMount(const std::string& path, const std::string& ufs_uri);
  bool DeleteMount(const std::string& path);
  void IterateMounts(std::function<void(const std::string& path,
                                        const std::string& ufs_uri)> fn);

  // Batched metadata updates for atomic apply paths.
  bool BatchCreateInode(InodeId id, const InodeEntry& entry, InodeId parent_id,
                        const std::string& child_name, InodeId child_id,
                        std::optional<InodeId> next_id = std::nullopt);
  bool BatchDeleteInode(InodeId id, InodeId parent_id,
                        const std::string& child_name);

  // Create a RocksDB checkpoint at the given path.
  bool CreateCheckpoint(const std::string& path);

  // Test-only failpoint for write operations.
  void FailNextWriteForTest(uint32_t count = 1);

 private:
  rocksdb::DB* db_ = nullptr;
  rocksdb::ColumnFamilyHandle* inodes_cf_ = nullptr;
  rocksdb::ColumnFamilyHandle* edges_cf_ = nullptr;
  rocksdb::ColumnFamilyHandle* mounts_cf_ = nullptr;
  uint32_t fail_next_write_count_ = 0;

  static std::string EncodeInodeKey(InodeId id);
  static std::string EncodeEdgeKey(InodeId parent_id, const std::string& child_name);
  static std::string EncodeInodeValue(const InodeEntry& e);
  static std::optional<InodeEntry> DecodeInodeValue(const std::string& value);
  static std::string EncodeEdgeValue(InodeId child_id);
  static InodeId DecodeEdgeValue(const std::string& value);
  bool MaybeFailWriteForTest();
};

}  // namespace fluxcache
