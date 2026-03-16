#include "master/inode_store.h"
#include <cstring>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/status.h>

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

// next_id stored in inodes CF with a reserved key (all-zero id would be invalid)
static const std::string kNextIdKey(8, '\0');

}  // namespace

bool InodeStore::Open(const std::string& path) {
  if (db_) return false;

  rocksdb::Options options;
  options.create_if_missing = true;
  options.create_missing_column_families = true;

  std::vector<rocksdb::ColumnFamilyDescriptor> column_families;
  column_families.emplace_back(rocksdb::kDefaultColumnFamilyName, rocksdb::ColumnFamilyOptions());
  column_families.emplace_back("inodes", rocksdb::ColumnFamilyOptions());
  column_families.emplace_back("edges", rocksdb::ColumnFamilyOptions());
  column_families.emplace_back("mounts", rocksdb::ColumnFamilyOptions());

  std::vector<rocksdb::ColumnFamilyHandle*> handles;
  rocksdb::DB* db = nullptr;
  rocksdb::Status s = rocksdb::DB::Open(options, path, column_families, &handles, &db);
  if (!s.ok()) {
    return false;
  }

  // handles[0] = default, [1] = inodes, [2] = edges, [3] = mounts
  db_ = db;
  inodes_cf_ = handles[1];
  edges_cf_ = handles[2];
  mounts_cf_ = handles[3];
  delete handles[0];

  return true;
}

void InodeStore::Close() {
  if (!db_) return;
  delete mounts_cf_;
  delete edges_cf_;
  delete inodes_cf_;
  mounts_cf_ = nullptr;
  edges_cf_ = nullptr;
  inodes_cf_ = nullptr;
  delete db_;
  db_ = nullptr;
}

std::string InodeStore::EncodeInodeKey(InodeId id) {
  std::string key(8, '\0');
  uint64_t be = HostToBigEndian(id);
  std::memcpy(&key[0], &be, 8);
  return key;
}

std::string InodeStore::EncodeEdgeKey(InodeId parent_id, const std::string& child_name) {
  std::string key(8, '\0');
  uint64_t be = HostToBigEndian(parent_id);
  std::memcpy(&key[0], &be, 8);
  key.append(child_name);
  return key;
}

std::string InodeStore::EncodeInodeValue(const InodeEntry& e) {
  std::string value;
  value.reserve(64 + e.name.size());
  uint64_t be_parent = HostToBigEndian(e.parent_id);
  value.append(reinterpret_cast<const char*>(&be_parent), 8);

  uint64_t be_size = HostToBigEndian(e.size);
  value.append(reinterpret_cast<const char*>(&be_size), 8);

  uint64_t be_block = HostToBigEndian(e.block_size);
  value.append(reinterpret_cast<const char*>(&be_block), 8);

  int64_t be_ct = HostToBigEndian(static_cast<uint64_t>(e.creation_time_ms));
  value.append(reinterpret_cast<const char*>(&be_ct), 8);

  int64_t be_mt = HostToBigEndian(static_cast<uint64_t>(e.modification_time_ms));
  value.append(reinterpret_cast<const char*>(&be_mt), 8);

  uint64_t be_ver = HostToBigEndian(e.file_version);
  value.append(reinterpret_cast<const char*>(&be_ver), 8);

  uint32_t mode = e.mode;
  uint8_t mode_buf[4] = {static_cast<uint8_t>((mode >> 24) & 0xff),
                         static_cast<uint8_t>((mode >> 16) & 0xff),
                         static_cast<uint8_t>((mode >> 8) & 0xff),
                         static_cast<uint8_t>(mode & 0xff)};
  value.append(reinterpret_cast<const char*>(mode_buf), 4);

  value.push_back(static_cast<char>(e.flags));
  value.push_back(static_cast<char>(e.owner_id));
  value.push_back(static_cast<char>(e.group_id));
  value.push_back(static_cast<char>(e._padding));

  value.append(e.name);
  return value;
}

std::optional<InodeEntry> InodeStore::DecodeInodeValue(const std::string& value) {
  if (value.size() < 52) return std::nullopt;  // 8+8+8+8+8+8+4+4 = 52
  InodeEntry e;
  uint64_t be;
  std::memcpy(&be, value.data(), 8);
  e.parent_id = BigEndianToHost(be);
  std::memcpy(&be, value.data() + 8, 8);
  e.size = BigEndianToHost(be);
  std::memcpy(&be, value.data() + 16, 8);
  e.block_size = BigEndianToHost(be);
  std::memcpy(&be, value.data() + 24, 8);
  e.creation_time_ms = static_cast<int64_t>(BigEndianToHost(be));
  std::memcpy(&be, value.data() + 32, 8);
  e.modification_time_ms = static_cast<int64_t>(BigEndianToHost(be));
  std::memcpy(&be, value.data() + 40, 8);
  e.file_version = BigEndianToHost(be);
  const uint8_t* p = reinterpret_cast<const uint8_t*>(value.data() + 48);
  e.mode = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
  e.flags = static_cast<uint8_t>(value[52]);
  e.owner_id = static_cast<uint8_t>(value[53]);
  e.group_id = static_cast<uint8_t>(value[54]);
  e._padding = static_cast<uint8_t>(value[55]);
  if (value.size() > 56) {
    e.name.assign(value.data() + 56, value.size() - 56);
  }
  return e;
}

std::string InodeStore::EncodeEdgeValue(InodeId child_id) {
  std::string value(8, '\0');
  uint64_t be = HostToBigEndian(child_id);
  std::memcpy(&value[0], &be, 8);
  return value;
}

InodeId InodeStore::DecodeEdgeValue(const std::string& value) {
  if (value.size() < 8) return 0;
  uint64_t be;
  std::memcpy(&be, value.data(), 8);
  return BigEndianToHost(be);
}

bool InodeStore::PutInode(InodeId id, const InodeEntry& entry) {
  if (!db_) return false;
  std::string key = EncodeInodeKey(id);
  std::string value = EncodeInodeValue(entry);
  rocksdb::Status s = db_->Put(rocksdb::WriteOptions(), inodes_cf_, key, value);
  return s.ok();
}

std::optional<InodeEntry> InodeStore::GetInode(InodeId id) {
  if (!db_) return std::nullopt;
  std::string key = EncodeInodeKey(id);
  std::string value;
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), inodes_cf_, key, &value);
  if (!s.ok()) return std::nullopt;
  return DecodeInodeValue(value);
}

bool InodeStore::DeleteInode(InodeId id) {
  if (!db_) return false;
  std::string key = EncodeInodeKey(id);
  rocksdb::Status s = db_->Delete(rocksdb::WriteOptions(), inodes_cf_, key);
  return s.ok();
}

bool InodeStore::PutEdge(InodeId parent_id, const std::string& child_name, InodeId child_id) {
  if (!db_) return false;
  std::string key = EncodeEdgeKey(parent_id, child_name);
  std::string value = EncodeEdgeValue(child_id);
  rocksdb::Status s = db_->Put(rocksdb::WriteOptions(), edges_cf_, key, value);
  return s.ok();
}

std::optional<InodeId> InodeStore::GetEdge(InodeId parent_id, const std::string& child_name) {
  if (!db_) return std::nullopt;
  std::string key = EncodeEdgeKey(parent_id, child_name);
  std::string value;
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), edges_cf_, key, &value);
  if (!s.ok()) return std::nullopt;
  return DecodeEdgeValue(value);
}

bool InodeStore::DeleteEdge(InodeId parent_id, const std::string& child_name) {
  if (!db_) return false;
  std::string key = EncodeEdgeKey(parent_id, child_name);
  rocksdb::Status s = db_->Delete(rocksdb::WriteOptions(), edges_cf_, key);
  return s.ok();
}

std::vector<std::pair<std::string, InodeId>> InodeStore::GetEdges(InodeId parent_id) {
  std::vector<std::pair<std::string, InodeId>> result;
  if (!db_) return result;

  std::string prefix = EncodeEdgeKey(parent_id, "");
  std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(rocksdb::ReadOptions(), edges_cf_));
  it->Seek(prefix);

  while (it->Valid()) {
    rocksdb::Slice k = it->key();
    if (k.size() <= 8) break;
    if (std::memcmp(k.data(), prefix.data(), 8) != 0) break;
    std::string name(k.data() + 8, k.size() - 8);
    InodeId child_id = DecodeEdgeValue(it->value().ToString());
    result.emplace_back(std::move(name), child_id);
    it->Next();
  }

  return result;
}

bool InodeStore::PutNextId(InodeId next) {
  if (!db_) return false;
  rocksdb::Status s =
      db_->Put(rocksdb::WriteOptions(), inodes_cf_, kNextIdKey, EncodeEdgeValue(next));
  return s.ok();
}

InodeId InodeStore::GetNextId() {
  if (!db_) return 1;
  std::string value;
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), inodes_cf_, kNextIdKey, &value);
  if (!s.ok()) return 1;
  return DecodeEdgeValue(value);
}

void InodeStore::IterateInodes(std::function<void(InodeId id, const InodeEntry& entry)> fn) {
  if (!db_) return;
  std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(rocksdb::ReadOptions(), inodes_cf_));
  it->SeekToFirst();
  while (it->Valid()) {
    rocksdb::Slice k = it->key();
    if (k.size() == 8) {
      uint64_t be;
      std::memcpy(&be, k.data(), 8);
      InodeId id = BigEndianToHost(be);
      if (id != 0) {
        auto entry = DecodeInodeValue(it->value().ToString());
        if (entry) fn(id, *entry);
      }
    }
    it->Next();
  }
}

void InodeStore::IterateAllEdges(
    std::function<void(InodeId parent_id, const std::string& child_name, InodeId child_id)> fn) {
  if (!db_) return;
  std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(rocksdb::ReadOptions(), edges_cf_));
  it->SeekToFirst();
  while (it->Valid()) {
    rocksdb::Slice k = it->key();
    if (k.size() > 8) {
      uint64_t be;
      std::memcpy(&be, k.data(), 8);
      InodeId parent_id = BigEndianToHost(be);
      std::string name(k.data() + 8, k.size() - 8);
      InodeId child_id = DecodeEdgeValue(it->value().ToString());
      fn(parent_id, name, child_id);
    }
    it->Next();
  }
}

bool InodeStore::PutMount(const std::string& path, const std::string& ufs_uri) {
  if (!db_ || !mounts_cf_) return false;
  rocksdb::Status s = db_->Put(rocksdb::WriteOptions(), mounts_cf_, path, ufs_uri);
  return s.ok();
}

bool InodeStore::DeleteMount(const std::string& path) {
  if (!db_ || !mounts_cf_) return false;
  rocksdb::Status s = db_->Delete(rocksdb::WriteOptions(), mounts_cf_, path);
  return s.ok();
}

void InodeStore::IterateMounts(
    std::function<void(const std::string& path, const std::string& ufs_uri)> fn) {
  if (!db_ || !mounts_cf_) return;
  std::unique_ptr<rocksdb::Iterator> it(
      db_->NewIterator(rocksdb::ReadOptions(), mounts_cf_));
  it->SeekToFirst();
  while (it->Valid()) {
    fn(it->key().ToString(), it->value().ToString());
    it->Next();
  }
}

}  // namespace fluxcache
