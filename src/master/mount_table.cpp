#include "master/mount_table.h"
#include "master/inode_store.h"

namespace fluxcache {

namespace {

bool IsSlash(char c) { return c == '/'; }

}  // namespace

void MountTable::BindStore(InodeStore* store) {
  store_ = store;
}

void MountTable::RecoverFromStore() {
  if (!store_) return;
  std::lock_guard<std::mutex> lock(mu_);
  store_->IterateMounts([this](const std::string& path,
                               const std::string& ufs_uri) {
    mounts_[path] = ufs_uri;
  });
}

std::string MountTable::NormalizePath(const std::string& path) {
  if (path.empty()) return "/";

  std::string result;
  result.reserve(path.size());
  bool prev_slash = false;

  for (size_t i = 0; i < path.size(); ++i) {
    if (IsSlash(path[i])) {
      if (!prev_slash) {
        result += '/';
        prev_slash = true;
      }
    } else {
      result += path[i];
      prev_slash = false;
    }
  }

  // Remove trailing slash unless it's the root "/"
  while (result.size() > 1 && result.back() == '/') {
    result.pop_back();
  }

  return result.empty() ? "/" : result;
}

bool MountTable::IsPrefixOf(const std::string& mount_path,
                            const std::string& logical_path) {
  if (mount_path.empty() || logical_path.empty()) return false;

  if (mount_path == "/") {
    return logical_path == "/" || (!logical_path.empty() && logical_path[0] == '/');
  }

  if (logical_path == mount_path) return true;
  if (logical_path.size() <= mount_path.size()) return false;

  return logical_path.compare(0, mount_path.size(), mount_path) == 0 &&
         logical_path[mount_path.size()] == '/';
}

Status MountTable::Mount(const std::string& path, const std::string& ufs_uri) {
  if (path.empty()) {
    return Status::InvalidArgument("Mount: path is required");
  }
  if (ufs_uri.empty()) {
    return Status::InvalidArgument("Mount: ufs_uri is required");
  }

  std::string norm = NormalizePath(path);

  std::lock_guard<std::mutex> lock(mu_);
  auto it = mounts_.find(norm);
  if (it != mounts_.end()) {
    return Status::InvalidArgument("Mount: path already mounted");
  }
  mounts_[norm] = ufs_uri;
  if (store_) store_->PutMount(norm, ufs_uri);
  return Status::OK();
}

Status MountTable::Unmount(const std::string& path) {
  if (path.empty()) {
    return Status::InvalidArgument("Unmount: path is required");
  }

  std::string norm = NormalizePath(path);

  std::lock_guard<std::mutex> lock(mu_);
  auto it = mounts_.find(norm);
  if (it == mounts_.end()) {
    return Status::NotFound("Unmount: no mount point for path");
  }
  mounts_.erase(it);
  if (store_) store_->DeleteMount(norm);
  return Status::OK();
}

Status MountTable::Resolve(const std::string& logical_path, std::string* ufs_uri,
                           std::string* ufs_path) const {
  if (!ufs_uri || !ufs_path) {
    return Status::InvalidArgument("Resolve: output pointers required");
  }

  std::string norm = NormalizePath(logical_path);
  if (norm.empty()) norm = "/";

  std::lock_guard<std::mutex> lock(mu_);

  // Iterate in reverse to find longest matching prefix first (map is ordered).
  for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it) {
    if (IsPrefixOf(it->first, norm)) {
      *ufs_uri = it->second;
      if (it->first == "/") {
        *ufs_path = (norm.size() > 1) ? norm.substr(1) : "";
      } else if (norm.size() == it->first.size()) {
        *ufs_path = "";
      } else {
        // norm.size() > it->first.size() and norm[it->first.size()] == '/'
        *ufs_path = norm.substr(it->first.size() + 1);
      }
      return Status::OK();
    }
  }

  return Status::NotFound("Resolve: no mount point for path");
}

std::vector<std::string> MountTable::ListMounts() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<std::string> result;
  result.reserve(mounts_.size());
  for (const auto& p : mounts_) {
    result.push_back(p.first);
  }
  return result;
}

}  // namespace fluxcache
