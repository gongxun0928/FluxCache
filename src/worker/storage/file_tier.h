#pragma once

#include "worker/storage/storage_tier.h"
#include <cstddef>
#include <string>
#include <unordered_map>

namespace fluxcache {

// File-based StorageTier implementation. Stores each block as block-{id}.dat.
// Supports recovery: on init, scans existing files to restore used_ and next_id_.
class FileTier : public StorageTier {
 public:
  // root_path: directory for block files. Created if not exists.
  // capacity_limit: max total bytes across all blocks.
  FileTier(std::string root_path, size_t capacity_limit);

  Status Allocate(size_t size, TierBlockHandle* handle) override;
  Status Write(const TierBlockHandle& handle, size_t offset,
               std::string_view data) override;
  Status Read(const TierBlockHandle& handle, size_t offset, size_t size,
              std::string* out) override;
  Status Release(const TierBlockHandle& handle) override;

  size_t UsedCapacity() const override { return used_; }
  size_t CapacityLimit() const override { return capacity_limit_; }

  const std::string& RootPath() const { return root_path_; }

 private:
  std::string BlockPath(uint64_t id) const;
  Status EnsureRootExists();
  Status RecoverFromDisk();

  std::string root_path_;
  size_t capacity_limit_;
  size_t used_{0};
  uint64_t next_id_{1};
  std::unordered_map<uint64_t, size_t> block_sizes_;
};

}  // namespace fluxcache
