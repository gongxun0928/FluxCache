#include "worker/meta/page_meta.h"
#include <cstring>

namespace fluxcache {

namespace {

inline uint64_t HostToBigEndian(uint64_t v) {
  uint64_t be;
  uint8_t* p = reinterpret_cast<uint8_t*>(&be);
  p[0] = (v >> 56) & 0xff;
  p[1] = (v >> 48) & 0xff;
  p[2] = (v >> 40) & 0xff;
  p[3] = (v >> 32) & 0xff;
  p[4] = (v >> 24) & 0xff;
  p[5] = (v >> 16) & 0xff;
  p[6] = (v >> 8) & 0xff;
  p[7] = v & 0xff;
  return be;
}

inline uint64_t BigEndianToHost(uint64_t be) {
  uint8_t* p = reinterpret_cast<uint8_t*>(&be);
  return (static_cast<uint64_t>(p[0]) << 56) | (static_cast<uint64_t>(p[1]) << 48) |
         (static_cast<uint64_t>(p[2]) << 40) | (static_cast<uint64_t>(p[3]) << 32) |
         (static_cast<uint64_t>(p[4]) << 24) | (static_cast<uint64_t>(p[5]) << 16) |
         (static_cast<uint64_t>(p[6]) << 8) | static_cast<uint64_t>(p[7]);
}

inline uint16_t HostToBigEndian16(uint16_t v) {
  uint16_t be;
  uint8_t* p = reinterpret_cast<uint8_t*>(&be);
  p[0] = (v >> 8) & 0xff;
  p[1] = v & 0xff;
  return be;
}

inline uint16_t BigEndianToHost16(uint16_t be) {
  uint8_t* p = reinterpret_cast<uint8_t*>(&be);
  return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

}  // namespace

std::string EncodePageMetaKey(BlockId block_id, uint16_t page_index) {
  std::string key(10, '\0');
  uint64_t be_block = HostToBigEndian(block_id);
  std::memcpy(&key[0], &be_block, 8);
  uint16_t be_page = HostToBigEndian16(page_index);
  std::memcpy(&key[8], &be_page, 2);
  return key;
}

bool DecodePageMetaKey(std::string_view key, BlockId* block_id, uint16_t* page_index) {
  if (key.size() < 10) return false;
  uint64_t be_block;
  std::memcpy(&be_block, key.data(), 8);
  *block_id = BigEndianToHost(be_block);
  uint16_t be_page;
  std::memcpy(&be_page, key.data() + 8, 2);
  *page_index = BigEndianToHost16(be_page);
  return true;
}

std::string EncodePageMetaValue(const PageMeta& meta) {
  std::string value(17, '\0');
  value[0] = static_cast<char>(meta.tier_type);
  uint64_t be_block = HostToBigEndian(meta.tier_block_id);
  std::memcpy(&value[1], &be_block, 8);
  int64_t be_mtime = HostToBigEndian(static_cast<uint64_t>(meta.cached_mtime_ms));
  std::memcpy(&value[9], &be_mtime, 8);
  return value;
}

std::optional<PageMeta> DecodePageMetaValue(std::string_view value) {
  if (value.size() < 17) return std::nullopt;
  PageMeta meta;
  meta.tier_type = static_cast<TierType>(static_cast<uint8_t>(value[0]));
  uint64_t be_block;
  std::memcpy(&be_block, value.data() + 1, 8);
  meta.tier_block_id = BigEndianToHost(be_block);
  int64_t be_mtime;
  std::memcpy(&be_mtime, value.data() + 9, 8);
  meta.cached_mtime_ms = static_cast<int64_t>(BigEndianToHost(static_cast<uint64_t>(be_mtime)));
  return meta;
}

}  // namespace fluxcache
