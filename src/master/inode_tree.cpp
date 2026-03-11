#include "master/inode_tree.h"
#include <chrono>
#include <sstream>

namespace fluxcache {

constexpr InodeId kRootInodeId = 1;

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
  next_id_ = store_->GetNextId();
  if (next_id_ < 2) next_id_ = 2;

  // 1) Load all directory inodes into DirNodes
  store_->IterateInodes([this](InodeId id, const InodeEntry& entry) {
    if (entry.is_directory()) {
      DirNode node;
      node.id = id;
      node.parent_id = entry.parent_id;
      node.name = entry.name;
      dirs_[id] = std::move(node);
    }
  });

  // 2) Fill children from edges
  store_->IterateAllEdges([this](InodeId parent_id, const std::string& child_name, InodeId child_id) {
    auto it = dirs_.find(parent_id);
    if (it != dirs_.end()) {
      it->second.children[child_name] = child_id;
    }
  });

  return true;
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

}  // namespace fluxcache
