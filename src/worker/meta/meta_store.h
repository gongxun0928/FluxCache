#pragma once

#include "common/types.h"
#include "worker/meta/page_meta.h"
#include <functional>
#include <optional>
#include <rocksdb/db.h>
#include <utility>
#include <string>

namespace fluxcache {

// RocksDB-backed page index for Worker recovery (design/metastore-design.md).
// Key: BlockId(8B) + PageIndex(2B) big-endian. Value: PageMeta.
class MetaStore {
 public:
  MetaStore() = default;
  ~MetaStore();
  MetaStore(const MetaStore&) = delete;
  MetaStore& operator=(const MetaStore&) = delete;

  bool Open(const std::string& path);
  void Close();
  bool is_open() const { return db_ != nullptr; }

  bool Put(PageId id, const PageMeta& meta);
  std::optional<PageMeta> Get(PageId id);
  bool Delete(PageId id);
  bool DeleteByBlock(BlockId block_id);

  void ScanAll(std::function<void(PageId id, const PageMeta& meta)> fn);
  void ScanBlock(BlockId block_id,
                 std::function<void(PageId id, const PageMeta& meta)> fn);

  // Paginated scan to avoid full ScanAll. Processes up to limit entries.
  // start_after: optional PageId to resume from (exclusive).
  // Returns number of entries processed.
  size_t ScanPaginated(
      std::optional<std::pair<BlockId, uint16_t>> start_after,
      size_t limit,
      std::function<void(PageId id, const PageMeta& meta)> fn);

 private:
  rocksdb::DB* db_ = nullptr;
  rocksdb::ColumnFamilyHandle* pages_cf_ = nullptr;
};

}  // namespace fluxcache
