#pragma once

#include "worker/storage/file_tier.h"
#include <cstddef>
#include <string>

namespace fluxcache {

// HDD tier: file-based storage for HDD-backed block files.
// Uses FileTier with the given root path and capacity limit.
class HddTier : public FileTier {
 public:
  HddTier(std::string root_path, size_t capacity_limit)
      : FileTier(std::move(root_path), capacity_limit, TierType::kHDD) {}
};

}  // namespace fluxcache
