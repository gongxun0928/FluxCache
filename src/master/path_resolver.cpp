#include "master/path_resolver.h"
#include "ufs/ufs_factory.h"

#include <queue>
#include <sstream>

namespace fluxcache {

namespace {

constexpr uint64_t kDefaultBlockSize = 64ULL * 1024 * 1024;  // 64MB

std::vector<std::string> SplitPath(const std::string& path) {
  std::vector<std::string> parts;
  if (path.empty() || path[0] != '/') return parts;
  std::string p = path;
  if (p.size() > 1 && p.back() == '/') p.pop_back();
  std::stringstream ss(p);
  std::string part;
  std::getline(ss, part, '/');
  while (std::getline(ss, part, '/')) {
    if (!part.empty()) parts.push_back(part);
  }
  return parts;
}

}  // namespace

PathResolver::PathResolver(InodeTree* tree, MountTable* mount_table)
    : tree_(tree), mount_table_(mount_table) {}

bool PathResolver::ParseUfsUri(const std::string& ufs_uri, std::string* scheme,
                                std::string* authority) {
  if (!scheme || !authority) return false;
  size_t pos = ufs_uri.find("://");
  if (pos == std::string::npos) return false;
  *scheme = ufs_uri.substr(0, pos);
  std::string rest = ufs_uri.substr(pos + 3);
  if (rest.size() >= 2 && rest[0] == '/' && rest[1] == '/') {
    *authority = rest.substr(1);
  } else {
    *authority = rest;
  }
  return true;
}

std::string PathResolver::Dirname(const std::string& path) {
  if (path.empty() || path == "/") return "/";
  size_t pos = path.rfind('/');
  if (pos == std::string::npos) return "/";
  if (pos == 0) return "/";
  return path.substr(0, pos);
}

std::string PathResolver::JoinPath(const std::string& a, const std::string& b) {
  if (a.empty() || a == "/") return b.empty() ? "/" : "/" + b;
  if (b.empty()) return a;
  return a + "/" + b;
}

Status PathResolver::SyncFromUfs(const std::string& logical_path) {
  if (!tree_ || !mount_table_) {
    return Status::InvalidArgument("PathResolver: tree and mount_table required");
  }
  if (!tree_->is_ready()) {
    return Status::IOError("PathResolver: InodeTree not ready");
  }

  std::string ufs_uri, ufs_path;
  Status s = mount_table_->Resolve(logical_path, &ufs_uri, &ufs_path);
  if (!s.ok()) return s;

  std::string scheme, authority;
  if (!ParseUfsUri(ufs_uri, &scheme, &authority)) {
    return Status::InvalidArgument("PathResolver: invalid ufs_uri");
  }

  std::unique_ptr<UFS> ufs;
  s = CreateUFS(scheme, authority, &ufs);
  if (!s.ok()) return s;
  if (!ufs) return Status::IOError("PathResolver: failed to create UFS");

  std::string mount_point =
      ufs_path.empty() ? logical_path
                       : logical_path.substr(0, logical_path.size() -
                                                    ufs_path.size() - 1);
  while (mount_point.size() > 1 && mount_point.back() == '/') {
    mount_point.pop_back();
  }
  if (mount_point.empty()) mount_point = "/";

  if (!tree_->LookupPath(mount_point).has_value()) {
    auto id = tree_->CreateDirectory(mount_point);
    if (!id.has_value()) {
      return Status::IOError("PathResolver: failed to create mount point");
    }
  }

  std::vector<std::string> rel_parts = SplitPath(ufs_path);
  std::string current_logical = mount_point;
  std::string current_ufs;

  for (size_t i = 0; i < rel_parts.size(); ++i) {
    const std::string& part = rel_parts[i];
    std::string child_logical = JoinPath(current_logical, part);
    std::string child_ufs = current_ufs.empty() ? part : current_ufs + "/" + part;

    if (tree_->LookupPath(child_logical).has_value()) {
      current_logical = child_logical;
      current_ufs = child_ufs;
      continue;
    }

    std::vector<FileStatus> entries;
    s = ufs->List(current_ufs, &entries);
    if (!s.ok()) return s;

    const FileStatus* found = nullptr;
    for (const auto& e : entries) {
      if (e.path == part) {
        found = &e;
        break;
      }
    }
    if (!found) return Status::NotFound("PathResolver: path not in UFS");

    if (found->is_directory) {
      auto id = tree_->CreateDirectory(child_logical);
      if (!id.has_value()) return Status::IOError("PathResolver: CreateDirectory failed");
    } else {
      auto id = tree_->CreateFile(child_logical, found->size, kDefaultBlockSize,
                                  found->mtime_ms);
      if (!id.has_value()) return Status::IOError("PathResolver: CreateFile failed");
    }
    current_logical = child_logical;
    current_ufs = child_ufs;
  }

  FileStatus target_status;
  std::string target_ufs = current_ufs;
  s = ufs->GetStatus(target_ufs, &target_status);
  if (s.ok() && target_status.is_directory) {
    std::vector<FileStatus> entries;
    s = ufs->List(target_ufs, &entries);
    if (!s.ok()) return s;
    for (const auto& e : entries) {
      std::string child_logical = JoinPath(current_logical, e.path);
      if (tree_->LookupPath(child_logical).has_value()) continue;
      if (e.is_directory) {
        if (!tree_->CreateDirectory(child_logical).has_value()) {
          return Status::IOError("PathResolver: CreateDirectory failed");
        }
      } else {
        if (!tree_->CreateFile(child_logical, e.size, kDefaultBlockSize,
                              e.mtime_ms)
                .has_value()) {
          return Status::IOError("PathResolver: CreateFile failed");
        }
      }
    }
  }

  return Status::OK();
}

std::optional<InodeId> PathResolver::ResolveOrSync(const std::string& logical_path) {
  if (!tree_) return std::nullopt;
  auto id = tree_->LookupPath(logical_path);
  if (id.has_value()) return id;

  Status s = SyncFromUfs(logical_path);
  if (!s.ok()) return std::nullopt;

  return tree_->LookupPath(logical_path);
}

std::optional<InodeId> PathResolver::ResolveLocal(
    const std::string& logical_path) const {
  if (!tree_) return std::nullopt;
  return tree_->LookupPath(logical_path);
}

std::vector<std::pair<std::string, InodeId>> PathResolver::ListDirectory(
    const std::string& logical_path) {
  if (!tree_) return {};
  Status s = SyncFromUfs(logical_path);
  if (!s.ok()) return {};

  auto id = tree_->LookupPath(logical_path);
  if (!id.has_value()) return {};

  return tree_->ListDirectory(*id);
}

Status PathResolver::PrewarmRecursive(const std::string& logical_path) {
  Status s = SyncFromUfs(logical_path);
  if (!s.ok()) return s;

  std::string ufs_uri, ufs_path;
  s = mount_table_->Resolve(logical_path, &ufs_uri, &ufs_path);
  if (!s.ok()) return s;

  std::string scheme, authority;
  if (!ParseUfsUri(ufs_uri, &scheme, &authority)) {
    return Status::InvalidArgument("PathResolver: invalid ufs_uri");
  }

  std::unique_ptr<UFS> ufs;
  s = CreateUFS(scheme, authority, &ufs);
  if (!s.ok()) return s;
  if (!ufs) return Status::IOError("PathResolver: failed to create UFS");

  std::string base_logical = logical_path;
  while (base_logical.size() > 1 && base_logical.back() == '/') {
    base_logical.pop_back();
  }
  std::string base_ufs = ufs_path;
  while (base_ufs.size() > 1 && base_ufs.back() == '/') {
    base_ufs.pop_back();
  }

  std::queue<std::pair<std::string, std::string>> q;
  q.push({base_logical, base_ufs});

  while (!q.empty()) {
    auto [cur_logical, cur_ufs] = q.front();
    q.pop();

    std::vector<FileStatus> entries;
    s = ufs->List(cur_ufs, &entries);
    if (!s.ok()) return s;

    for (const auto& e : entries) {
      std::string child_logical = JoinPath(cur_logical, e.path);
      if (tree_->LookupPath(child_logical).has_value()) continue;

      if (e.is_directory) {
        if (!tree_->CreateDirectory(child_logical).has_value()) {
          return Status::IOError("PathResolver: CreateDirectory failed");
        }
        std::string child_ufs =
            cur_ufs.empty() ? e.path : cur_ufs + "/" + e.path;
        q.push({child_logical, child_ufs});
      } else {
        if (!tree_->CreateFile(child_logical, e.size, kDefaultBlockSize,
                              e.mtime_ms)
                .has_value()) {
          return Status::IOError("PathResolver: CreateFile failed");
        }
      }
    }
  }

  return Status::OK();
}

}  // namespace fluxcache
