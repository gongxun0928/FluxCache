#pragma once

#include "common/types.h"
#include <cstdint>
#include <optional>
#include <string>

namespace fluxcache {

// Page metadata persisted in MetaStore (design/metastore-design.md).
struct PageMeta {
  TierType tier_type = TierType::kMemory;
  uint64_t tier_block_id = 0;
  uint64_t cached_file_version = 0;
};

// Key: BlockId(8B) + PageIndex(2B), big-endian.
std::string EncodePageMetaKey(BlockId block_id, uint16_t page_index);
bool DecodePageMetaKey(std::string_view key, BlockId* block_id, uint16_t* page_index);

// Value: tier_type(1B) + tier_block_id(8B) + cached_file_version(8B).
std::string EncodePageMetaValue(const PageMeta& meta);
std::optional<PageMeta> DecodePageMetaValue(std::string_view value);

}  // namespace fluxcache
