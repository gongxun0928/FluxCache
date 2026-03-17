#include "master/inode_tree.h"
#include <chrono>
#include <filesystem>
#include <sstream>

namespace fluxcache {

constexpr InodeId kRootInodeId = 1;
namespace fs = std::filesystem;

namespace {

bool BuildRecoveredView(
    InodeStore* store, std::unordered_map<InodeId, DirNode>* dirs_out,
    InodeId* next_id_out) {
  if (!store || !dirs_out || !next_id_out) return false;

  dirs_out->clear();
  *next_id_out = store->GetNextId();
  if (*next_id_out < 2) *next_id_out = 2;

  store->IterateInodes([dirs_out](InodeId id, const InodeEntry& entry) {
    if (!entry.is_directory()) return;
    DirNode node;
    node.id = id;
    node.parent_id = entry.parent_id;
    node.name = entry.name;
    (*dirs_out)[id] = std::move(node);
  });

  store->IterateAllEdges(
      [dirs_out](InodeId parent_id, const std::string& child_name,
                 InodeId child_id) {
        auto it = dirs_out->find(parent_id);
        if (it != dirs_out->end()) {
          it->second.children[child_name] = child_id;
        }
      });

  return dirs_out->count(kRootInodeId) > 0;
}

}  // namespace

InodeTree::InodeTree(const std::string& db_path) : db_path_(db_path) {
  store_ = std::make_unique<InodeStore>();
}

InodeTree::~InodeTree() {
  if (store_) {
    store_->Close();
  }
}

bool InodeTree::InitOrRecover() {
  std::unique_lock lock(mu_);
  if (!store_->Open(db_path_)) {
    return false;
  }

  InodeId next = store_->GetNextId();
  if (next == 1) {
    if (!InitRoot()) {
      return false;
    }
  } else {
    if (!Recover()) {
      return false;
    }
  }
  ready_ = true;
  return true;
}

bool InodeTree::InitRoot() {
  if (store_->GetInode(kRootInodeId)) {
    return true;
  }

  int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  InodeEntry root;
  root.parent_id = 0;
  root.size = 0;
  root.block_size = 0;
  root.creation_time_ms = now;
  root.modification_time_ms = now;
  root.file_version = 0;
  root.set_directory(true);
  root.name = "/";

  if (!store_->PutInode(kRootInodeId, root)) return false;
  if (!store_->PutNextId(2)) return false;

  DirNode root_node;
  root_node.id = kRootInodeId;
  root_node.parent_id = 0;
  root_node.name = "/";
  dirs_[kRootInodeId] = std::move(root_node);
  next_id_ = 2;

  return true;
}

bool InodeTree::Recover() {
  return BuildRecoveredView(store_.get(), &dirs_, &next_id_);
}

std::vector<std::string> InodeTree::SplitPath(const std::string& path) const {
  std::vector<std::string> parts;
  if (path.empty() || path[0] != '/') return parts;
  std::string p = path;
  if (p.size() > 1 && p.back() == '/') p.pop_back();
  std::stringstream ss(p);
  std::string part;
  std::getline(ss, part, '/');  // skip leading empty
  while (std::getline(ss, part, '/')) {
    if (!part.empty()) parts.push_back(part);
  }
  return parts;
}

DirNode* InodeTree::GetDirNode(InodeId id) {
  auto it = dirs_.find(id);
  return it != dirs_.end() ? &it->second : nullptr;
}

const DirNode* InodeTree::GetDirNode(InodeId id) const {
  auto it = dirs_.find(id);
  return it != dirs_.end() ? &it->second : nullptr;
}

std::optional<InodeId> InodeTree::LookupPath(const std::string& path) const {
  std::shared_lock lock(mu_);
  if (!ready_) return std::nullopt;

  std::vector<std::string> parts = SplitPath(path);
  if (parts.empty()) {
    return kRootInodeId;  // "/" or "/"
  }

  const DirNode* current = GetDirNode(kRootInodeId);
  if (!current) return std::nullopt;

  for (size_t i = 0; i < parts.size(); ++i) {
    auto it = current->children.find(parts[i]);
    if (it == current->children.end()) return std::nullopt;
    InodeId child_id = it->second;
    if (i + 1 == parts.size()) {
      return child_id;
    }
    current = GetDirNode(child_id);
    if (!current) return std::nullopt;
  }
  return std::nullopt;
}

std::optional<InodeId> InodeTree::CreateFile(const std::string& path) {
  std::unique_lock lock(mu_);
  if (!ready_) return std::nullopt;

  std::vector<std::string> parts = SplitPath(path);
  if (parts.empty()) return std::nullopt;

  std::string name = parts.back();
  parts.pop_back();

  InodeId parent_id = kRootInodeId;
  for (const auto& p : parts) {
    const DirNode* cur = GetDirNode(parent_id);
    if (!cur) return std::nullopt;
    auto it = cur->children.find(p);
    if (it == cur->children.end()) return std::nullopt;
    parent_id = it->second;
  }

  DirNode* parent = GetDirNode(parent_id);
  if (!parent) return std::nullopt;
  if (parent->children.count(name)) return std::nullopt;

  InodeId id = next_id_++;
  int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  InodeEntry entry;
  entry.parent_id = parent_id;
  entry.size = 0;
  entry.block_size = 0;
  entry.creation_time_ms = now;
  entry.modification_time_ms = now;
  entry.file_version = 0;
  entry.set_directory(false);
  entry.name = name;

  if (!store_->PutInode(id, entry)) return std::nullopt;
  if (!store_->PutEdge(parent_id, name, id)) return std::nullopt;
  if (!store_->PutNextId(next_id_)) return std::nullopt;

  parent->children[name] = id;
  return id;
}

std::optional<InodeId> InodeTree::CreateFile(const std::string& path,
                                            uint64_t size, uint64_t block_size,
                                            int64_t mtime_ms) {
  std::unique_lock lock(mu_);
  if (!ready_) return std::nullopt;

  std::vector<std::string> parts = SplitPath(path);
  if (parts.empty()) return std::nullopt;

  std::string name = parts.back();
  parts.pop_back();

  InodeId parent_id = kRootInodeId;
  for (const auto& p : parts) {
    const DirNode* cur = GetDirNode(parent_id);
    if (!cur) return std::nullopt;
    auto it = cur->children.find(p);
    if (it == cur->children.end()) return std::nullopt;
    parent_id = it->second;
  }

  DirNode* parent = GetDirNode(parent_id);
  if (!parent) return std::nullopt;
  if (parent->children.count(name)) return std::nullopt;

  InodeId id = next_id_++;
  int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  InodeEntry entry;
  entry.parent_id = parent_id;
  entry.size = size;
  entry.block_size = block_size;
  entry.creation_time_ms = now;
  entry.modification_time_ms = mtime_ms;
  entry.file_version = 0;
  entry.set_directory(false);
  entry.name = name;

  if (!store_->PutInode(id, entry)) return std::nullopt;
  if (!store_->PutEdge(parent_id, name, id)) return std::nullopt;
  if (!store_->PutNextId(next_id_)) return std::nullopt;

  parent->children[name] = id;
  return id;
}

std::optional<InodeId> InodeTree::CreateDirectory(const std::string& path) {
  std::unique_lock lock(mu_);
  if (!ready_) return std::nullopt;

  std::vector<std::string> parts = SplitPath(path);
  if (parts.empty()) return std::nullopt;

  std::string name = parts.back();
  parts.pop_back();

  InodeId parent_id = kRootInodeId;
  for (const auto& p : parts) {
    const DirNode* cur = GetDirNode(parent_id);
    if (!cur) return std::nullopt;
    auto it = cur->children.find(p);
    if (it == cur->children.end()) return std::nullopt;
    parent_id = it->second;
  }

  DirNode* parent = GetDirNode(parent_id);
  if (!parent) return std::nullopt;
  if (parent->children.count(name)) return std::nullopt;

  InodeId id = next_id_++;
  int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  InodeEntry entry;
  entry.parent_id = parent_id;
  entry.size = 0;
  entry.block_size = 0;
  entry.creation_time_ms = now;
  entry.modification_time_ms = now;
  entry.file_version = 0;
  entry.set_directory(true);
  entry.name = name;

  if (!store_->PutInode(id, entry)) return std::nullopt;
  if (!store_->PutEdge(parent_id, name, id)) return std::nullopt;
  if (!store_->PutNextId(next_id_)) return std::nullopt;

  parent->children[name] = id;

  DirNode node;
  node.id = id;
  node.parent_id = parent_id;
  node.name = name;
  dirs_[id] = std::move(node);

  return id;
}

bool InodeTree::DeleteInode(InodeId id) {
  std::unique_lock lock(mu_);
  if (!ready_) return false;

  auto entry = store_->GetInode(id);
  if (!entry) return false;

  InodeId parent_id = entry->parent_id;
  std::string name = entry->name;

  if (entry->is_directory()) {
    DirNode* node = GetDirNode(id);
    if (!node || !node->children.empty()) return false;
    dirs_.erase(id);
  }

  DirNode* parent = GetDirNode(parent_id);
  if (!parent) return false;
  if (parent->children.erase(name) == 0) return false;

  if (!store_->DeleteEdge(parent_id, name)) return false;
  if (!store_->DeleteInode(id)) return false;

  return true;
}

std::vector<std::pair<std::string, InodeId>> InodeTree::ListDirectory(InodeId dir_id) {
  std::shared_lock lock(mu_);
  if (!ready_) return {};

  const DirNode* node = GetDirNode(dir_id);
  if (!node) return {};

  std::vector<std::pair<std::string, InodeId>> result;
  for (const auto& [name, id] : node->children) {
    result.emplace_back(name, id);
  }
  return result;
}

std::optional<InodeEntry> InodeTree::GetInode(InodeId id) {
  std::shared_lock lock(mu_);
  if (!ready_) return std::nullopt;
  return store_->GetInode(id);
}

bool InodeTree::UpdateInodeSizeAndMtime(InodeId id, uint64_t size,
                                        int64_t mtime_ms) {
  std::unique_lock lock(mu_);
  if (!ready_) return false;

  auto entry = store_->GetInode(id);
  if (!entry) return false;
  if (entry->is_directory()) return false;

  entry->size = size;
  entry->modification_time_ms = mtime_ms;
  return store_->PutInode(id, *entry);
}

bool InodeTree::HasInodesUnderPath(const std::string& prefix) const {
  return LookupPath(prefix).has_value();
}

bool InodeTree::CreateCheckpoint(const std::string& path) {
  std::shared_lock lock(mu_);
  if (!ready_ || !store_) return false;
  return store_->CreateCheckpoint(path);
}

bool InodeTree::RestoreFromCheckpoint(const std::string& checkpoint_path) {
  std::unique_lock lock(mu_);
  if (!store_) return false;

  auto restore_current_db = [this]() -> bool {
    dirs_.clear();
    next_id_ = 2;
    if (!store_->Open(db_path_)) return false;
    return Recover();
  };

  std::error_code ec;
  const fs::path incoming_path = db_path_ + ".incoming";
  const fs::path backup_path = db_path_ + ".bak";
  fs::remove_all(incoming_path, ec);
  fs::remove_all(backup_path, ec);

  fs::copy(checkpoint_path, incoming_path,
           fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  if (ec) return false;

  std::unordered_map<InodeId, DirNode> incoming_dirs;
  InodeId incoming_next_id = 2;
  {
    InodeStore incoming_store;
    if (!incoming_store.Open(incoming_path.string())) {
      fs::remove_all(incoming_path, ec);
      return false;
    }
    if (!BuildRecoveredView(&incoming_store, &incoming_dirs, &incoming_next_id)) {
      incoming_store.Close();
      fs::remove_all(incoming_path, ec);
      return false;
    }
    incoming_store.Close();
  }

  store_->Close();
  ready_ = false;

  bool had_existing_db = fs::exists(db_path_);
  if (had_existing_db) {
    fs::rename(db_path_, backup_path, ec);
    if (ec) {
      ready_ = restore_current_db();
      fs::remove_all(incoming_path, ec);
      return false;
    }
  }

  fs::rename(incoming_path, db_path_, ec);
  if (ec) {
    if (had_existing_db) {
      std::error_code rollback_ec;
      fs::rename(backup_path, db_path_, rollback_ec);
      ready_ = restore_current_db();
    }
    fs::remove_all(incoming_path, ec);
    return false;
  }

  if (!store_->Open(db_path_)) {
    std::error_code rollback_ec;
    store_->Close();
    fs::remove_all(db_path_, rollback_ec);
    dirs_.clear();
    next_id_ = 2;
    if (had_existing_db) {
      fs::rename(backup_path, db_path_, rollback_ec);
      ready_ = restore_current_db();
    }
    return false;
  }

  dirs_ = std::move(incoming_dirs);
  next_id_ = incoming_next_id;
  ready_ = true;
  fs::remove_all(backup_path, ec);
  return true;
}

InodeTree::AllocResult InodeTree::AllocateInodeId() {
  std::unique_lock lock(mu_);
  InodeId id = next_id_++;
  return {id, next_id_};
}

std::optional<InodeId> InodeTree::FindParentId(const std::string& path) const {
  std::shared_lock lock(mu_);
  if (!ready_) return std::nullopt;
  std::vector<std::string> parts = SplitPath(path);
  if (parts.empty()) return std::nullopt;
  parts.pop_back();

  InodeId parent_id = kRootInodeId;
  for (const auto& p : parts) {
    const DirNode* cur = GetDirNode(parent_id);
    if (!cur) return std::nullopt;
    auto it = cur->children.find(p);
    if (it == cur->children.end()) return std::nullopt;
    parent_id = it->second;
  }
  return parent_id;
}

Status InodeTree::ApplyCreateFile(InodeId id, const std::string& path,
                                  InodeId parent_id, uint64_t size,
                                  uint64_t block_size,
                                  int64_t creation_time_ms, int64_t mtime_ms,
                                  InodeId next_id) {
  std::unique_lock lock(mu_);
  if (!ready_) return Status::Unavailable("ApplyCreateFile: InodeTree not ready");

  std::vector<std::string> parts = SplitPath(path);
  if (parts.empty()) return Status::InvalidArgument("ApplyCreateFile: invalid path");
  std::string name = parts.back();

  DirNode* parent = GetDirNode(parent_id);
  if (!parent) return Status::NotFound("ApplyCreateFile: parent not found");
  if (parent->children.count(name)) {
    return Status::AlreadyExists("ApplyCreateFile: path already exists");
  }

  InodeEntry entry;
  entry.parent_id = parent_id;
  entry.size = size;
  entry.block_size = block_size;
  entry.creation_time_ms = creation_time_ms;
  entry.modification_time_ms = mtime_ms;
  entry.file_version = 0;
  entry.set_directory(false);
  entry.name = name;

  std::optional<InodeId> persisted_next_id;
  if (next_id > next_id_) {
    persisted_next_id = next_id;
  }
  if (!store_->BatchCreateInode(id, entry, parent_id, name, id, persisted_next_id)) {
    return Status::IOError("ApplyCreateFile: failed to persist create batch");
  }

  if (persisted_next_id.has_value()) next_id_ = *persisted_next_id;
  parent->children[name] = id;
  return Status::OK();
}

Status InodeTree::ApplyCreateDirectory(InodeId id, const std::string& path,
                                       InodeId parent_id,
                                       int64_t creation_time_ms,
                                       int64_t modification_time_ms,
                                       InodeId next_id) {
  std::unique_lock lock(mu_);
  if (!ready_) {
    return Status::Unavailable("ApplyCreateDirectory: InodeTree not ready");
  }

  std::vector<std::string> parts = SplitPath(path);
  if (parts.empty()) {
    return Status::InvalidArgument("ApplyCreateDirectory: invalid path");
  }
  std::string name = parts.back();

  DirNode* parent = GetDirNode(parent_id);
  if (!parent) {
    return Status::NotFound("ApplyCreateDirectory: parent not found");
  }
  if (parent->children.count(name)) {
    return Status::AlreadyExists("ApplyCreateDirectory: path already exists");
  }

  InodeEntry entry;
  entry.parent_id = parent_id;
  entry.size = 0;
  entry.block_size = 0;
  entry.creation_time_ms = creation_time_ms;
  entry.modification_time_ms = modification_time_ms;
  entry.file_version = 0;
  entry.set_directory(true);
  entry.name = name;

  std::optional<InodeId> persisted_next_id;
  if (next_id > next_id_) {
    persisted_next_id = next_id;
  }
  if (!store_->BatchCreateInode(id, entry, parent_id, name, id, persisted_next_id)) {
    return Status::IOError("ApplyCreateDirectory: failed to persist create batch");
  }

  if (persisted_next_id.has_value()) next_id_ = *persisted_next_id;
  parent->children[name] = id;

  DirNode node;
  node.id = id;
  node.parent_id = parent_id;
  node.name = name;
  dirs_[id] = std::move(node);

  return Status::OK();
}

Status InodeTree::ApplyDeleteInode(InodeId id) {
  std::unique_lock lock(mu_);
  if (!ready_) return Status::Unavailable("ApplyDeleteInode: InodeTree not ready");

  auto entry = store_->GetInode(id);
  if (!entry) return Status::NotFound("ApplyDeleteInode: inode not found");

  InodeId parent_id = entry->parent_id;
  std::string name = entry->name;

  if (entry->is_directory()) {
    DirNode* node = GetDirNode(id);
    if (!node || !node->children.empty()) {
      return Status::InvalidArgument("ApplyDeleteInode: directory not empty");
    }
  }

  DirNode* parent = GetDirNode(parent_id);
  if (!parent) return Status::NotFound("ApplyDeleteInode: parent not found");
  if (parent->children.find(name) == parent->children.end()) {
    return Status::NotFound("ApplyDeleteInode: inode edge not found");
  }
  if (!store_->BatchDeleteInode(id, parent_id, name)) {
    return Status::IOError("ApplyDeleteInode: failed to persist delete batch");
  }
  parent->children.erase(name);
  if (entry->is_directory()) {
    dirs_.erase(id);
  }

  return Status::OK();
}

Status InodeTree::ApplyUpdateSizeAndMtime(InodeId id, uint64_t size,
                                          int64_t mtime_ms) {
  std::unique_lock lock(mu_);
  if (!ready_) {
    return Status::Unavailable("ApplyUpdateSizeAndMtime: InodeTree not ready");
  }

  auto entry = store_->GetInode(id);
  if (!entry) return Status::NotFound("ApplyUpdateSizeAndMtime: inode not found");
  if (entry->is_directory()) {
    return Status::InvalidArgument(
        "ApplyUpdateSizeAndMtime: inode is a directory");
  }

  entry->size = size;
  entry->modification_time_ms = mtime_ms;
  if (!store_->PutInode(id, *entry)) {
    return Status::IOError("ApplyUpdateSizeAndMtime: failed to persist inode");
  }
  return Status::OK();
}

}  // namespace fluxcache
