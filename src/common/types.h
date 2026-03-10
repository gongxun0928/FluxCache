#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace fluxcache {

// -----------------------------------------------------------------------------
// Core ID types
// -----------------------------------------------------------------------------

using InodeId = uint64_t;
using BlockId = uint64_t;
using WorkerId = uint64_t;

// -----------------------------------------------------------------------------
// BlockId bit layout (design/block-id-and-file-layout.md)
//   InodeId:   40 bits [63:24]
//   BlockIndex: 24 bits [23:0]
// -----------------------------------------------------------------------------

constexpr int kInodeIdBits = 40;
constexpr int kBlockIndexBits = 24;
constexpr BlockId kInvalidBlockId = 0;
constexpr InodeId kInvalidInodeId = 0;

constexpr InodeId kMaxInodeId = (InodeId{1} << kInodeIdBits) - 1;
constexpr uint32_t kMaxBlockIndex = (uint32_t{1} << kBlockIndexBits) - 1;

inline BlockId MakeBlockId(InodeId inode_id, uint32_t block_index) {
  return (static_cast<BlockId>(inode_id) << kBlockIndexBits) |
         static_cast<BlockId>(block_index);
}

inline InodeId GetInodeId(BlockId block_id) {
  return static_cast<InodeId>(block_id >> kBlockIndexBits);
}

inline uint32_t GetBlockIndex(BlockId block_id) {
  return static_cast<uint32_t>(block_id & ((BlockId{1} << kBlockIndexBits) - 1));
}

inline uint32_t GetBlockCount(uint64_t file_size, size_t block_size) {
  if (block_size == 0) return 0;
  return static_cast<uint32_t>((file_size + block_size - 1) / block_size);
}

inline size_t GetBlockLength(uint64_t file_size, uint32_t block_index,
                            size_t block_size) {
  if (block_size == 0) return 0;
  uint32_t block_count = GetBlockCount(file_size, block_size);
  if (block_index >= block_count) return 0;
  uint64_t offset = static_cast<uint64_t>(block_index) * block_size;
  uint64_t remaining = file_size - offset;
  return static_cast<size_t>(remaining < block_size ? remaining : block_size);
}

// -----------------------------------------------------------------------------
// PageId (design/block-id-and-file-layout.md §4.1)
// -----------------------------------------------------------------------------

struct PageId {
  BlockId block_id = kInvalidBlockId;
  uint16_t page_index = 0;

  PageId() = default;
  PageId(BlockId bid, uint16_t pidx) : block_id(bid), page_index(pidx) {}

  bool operator==(const PageId& o) const {
    return block_id == o.block_id && page_index == o.page_index;
  }
};

inline PageId MakePageId(BlockId block_id, uint16_t page_index) {
  return PageId(block_id, page_index);
}

inline uint32_t GetPagesPerBlock(size_t block_size, size_t page_size) {
  if (page_size == 0) return 0;
  return static_cast<uint32_t>(block_size / page_size);
}

// -----------------------------------------------------------------------------
// TierType (Memory / SSD / HDD storage tiers)
// -----------------------------------------------------------------------------

enum class TierType : uint8_t {
  kMemory = 0,
  kSSD = 1,
  kHDD = 2,
};

// -----------------------------------------------------------------------------
// WorkerState (design/metadata-design.md §2.3)
// -----------------------------------------------------------------------------

enum class WorkerState : uint8_t {
  kAlive = 0,
  kSuspect = 1,
  kDead = 2,
};

}  // namespace fluxcache

// -----------------------------------------------------------------------------
// std::hash<PageId> for unordered_map
// -----------------------------------------------------------------------------

namespace std {

template <>
struct hash<fluxcache::PageId> {
  size_t operator()(const fluxcache::PageId& p) const noexcept {
    return std::hash<fluxcache::BlockId>{}(p.block_id) ^
           (std::hash<uint16_t>{}(p.page_index) << 1);
  }
};

}  // namespace std
