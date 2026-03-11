#pragma once

#include "worker/storage/file_tier.h"
#include <cstddef>
#include <string>

namespace fluxcache {

// SSD tier: file-based storage for SSD-backed block files.
// Uses FileTier with the given root path and capacity limit.
class SsdTier : public FileTier {
 public:
  SsdTier(std::string root_path, size_t capacity_limit)
      : FileTier(std::move(root_path), capacity_limit, TierType::kSSD) {}
};

}  // namespace fluxcache
