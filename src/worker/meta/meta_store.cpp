#include "worker/meta/meta_store.h"
#include <cstring>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/status.h>

namespace fluxcache {

namespace {

inline uint64_t BigEndianToHost(uint64_t be) {
  uint8_t* p = reinterpret_cast<uint8_t*>(&be);
  return (static_cast<uint64_t>(p[0]) << 56) | (static_cast<uint64_t>(p[1]) << 48) |
         (static_cast<uint64_t>(p[2]) << 40) | (static_cast<uint64_t>(p[3]) << 32) |
         (static_cast<uint64_t>(p[4]) << 24) | (static_cast<uint64_t>(p[5]) << 16) |
         (static_cast<uint64_t>(p[6]) << 8) | static_cast<uint64_t>(p[7]);
}

}  // namespace

MetaStore::~MetaStore() {
  Close();
}

bool MetaStore::Open(const std::string& path) {
  if (db_) return false;

  rocksdb::Options options;
  options.create_if_missing = true;
  options.create_missing_column_families = true;

  std::vector<rocksdb::ColumnFamilyDescriptor> column_families;
  column_families.emplace_back(rocksdb::kDefaultColumnFamilyName,
                              rocksdb::ColumnFamilyOptions());
  column_families.emplace_back("pages", rocksdb::ColumnFamilyOptions());

  std::vector<rocksdb::ColumnFamilyHandle*> handles;
  rocksdb::DB* db = nullptr;
  rocksdb::Status s =
      rocksdb::DB::Open(options, path, column_families, &handles, &db);
  if (!s.ok()) return false;

  db_ = db;
  pages_cf_ = handles[1];
  delete handles[0];
  return true;
}

void MetaStore::Close() {
  if (!db_) return;
  delete pages_cf_;
  pages_cf_ = nullptr;
  delete db_;
  db_ = nullptr;
}

bool MetaStore::Put(PageId id, const PageMeta& meta) {
  if (!db_) return false;
  std::string key = EncodePageMetaKey(id.block_id, id.page_index);
  std::string value = EncodePageMetaValue(meta);
  rocksdb::Status s = db_->Put(rocksdb::WriteOptions(), pages_cf_, key, value);
  return s.ok();
}

std::optional<PageMeta> MetaStore::Get(PageId id) {
  if (!db_) return std::nullopt;
  std::string key = EncodePageMetaKey(id.block_id, id.page_index);
  std::string value;
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), pages_cf_, key, &value);
  if (!s.ok()) return std::nullopt;
  return DecodePageMetaValue(value);
}

bool MetaStore::Delete(PageId id) {
  if (!db_) return false;
  std::string key = EncodePageMetaKey(id.block_id, id.page_index);
  rocksdb::Status s = db_->Delete(rocksdb::WriteOptions(), pages_cf_, key);
  return s.ok();
}

bool MetaStore::DeleteByBlock(BlockId block_id) {
  if (!db_) return false;
  std::string prefix = EncodePageMetaKey(block_id, 0);
  std::unique_ptr<rocksdb::Iterator> it(
      db_->NewIterator(rocksdb::ReadOptions(), pages_cf_));
  it->Seek(prefix);

  rocksdb::WriteBatch batch;
  while (it->Valid()) {
    rocksdb::Slice k = it->key();
    if (k.size() < 10) break;
    uint64_t be_block;
    std::memcpy(&be_block, k.data(), 8);
    BlockId bid = BigEndianToHost(be_block);
    if (bid != block_id) break;
    batch.Delete(pages_cf_, k);
    it->Next();
  }

  rocksdb::Status s = db_->Write(rocksdb::WriteOptions(), &batch);
  return s.ok();
}

void MetaStore::ScanAll(std::function<void(PageId id, const PageMeta& meta)> fn) {
  if (!db_) return;
  std::unique_ptr<rocksdb::Iterator> it(
      db_->NewIterator(rocksdb::ReadOptions(), pages_cf_));
  it->SeekToFirst();
  while (it->Valid()) {
    rocksdb::Slice k = it->key();
    if (k.size() >= 10) {
      BlockId block_id;
      uint16_t page_index;
      if (DecodePageMetaKey(std::string_view(k.data(), k.size()), &block_id,
                            &page_index)) {
        auto meta = DecodePageMetaValue(it->value().ToString());
        if (meta) {
          fn(PageId{block_id, page_index}, *meta);
        }
      }
    }
    it->Next();
  }
}

void MetaStore::ScanBlock(
    BlockId block_id,
    std::function<void(PageId id, const PageMeta& meta)> fn) {
  if (!db_) return;
  std::string prefix = EncodePageMetaKey(block_id, 0);
  std::unique_ptr<rocksdb::Iterator> it(
      db_->NewIterator(rocksdb::ReadOptions(), pages_cf_));
  it->Seek(prefix);
  while (it->Valid()) {
    rocksdb::Slice k = it->key();
    if (k.size() < 10) break;
    BlockId bid;
    uint16_t page_index;
    if (!DecodePageMetaKey(std::string_view(k.data(), k.size()), &bid,
                           &page_index))
      break;
    if (bid != block_id) break;
    auto meta = DecodePageMetaValue(it->value().ToString());
    if (meta) {
      fn(PageId{block_id, page_index}, *meta);
    }
    it->Next();
  }
}

}  // namespace fluxcache
