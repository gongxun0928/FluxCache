#include "ufs/fake_ufs.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace fluxcache {

void FakeUfs::AddFile(const std::string& path, uint64_t size, int64_t mtime_ms) {
  FileStatus fs;
  fs.exists = true;
  fs.is_directory = false;
  fs.size = size;
  fs.mtime_ms = mtime_ms;
  fs.path = path;
  files_[path] = std::move(fs);
}

void FakeUfs::AddFileWithContent(const std::string& path,
                                 const std::string& content, int64_t mtime_ms) {
  AddFile(path, static_cast<uint64_t>(content.size()), mtime_ms);
  content_[path] = content;
}

std::unique_ptr<UFS> FakeUfs::Clone() const {
  auto clone = std::make_unique<FakeUfs>();
  clone->files_ = files_;
  clone->content_ = content_;
  clone->read_count_ = read_count_;
  return clone;
}

Status FakeUfs::Read(const std::string& path, uint64_t offset, uint64_t size,
                     std::string* out) {
  if (!out) return Status::InvalidArgument(nullptr);
  auto it = content_.find(path);
  if (it == content_.end()) {
    return Status::NotFound("FakeUfs: no content for path");
  }
  const std::string& data = it->second;
  if (read_count_) read_count_->fetch_add(1);
  if (offset >= data.size()) {
    out->clear();
    return Status::OK();
  }
  size_t to_read = std::min(size, data.size() - offset);
  out->assign(data.data() + offset, to_read);
  return Status::OK();
}

Status FakeUfs::Write(const std::string& path, uint64_t offset,
                     std::string_view data) {
  (void)path;
  (void)offset;
  (void)data;
  return Status::InvalidArgument("FakeUfs::Write not implemented");
}

Status FakeUfs::GetStatus(const std::string& path, FileStatus* status) {
  if (!status) return Status::InvalidArgument(nullptr);
  *status = FileStatus{};
  status->path = path;
  if (path.empty() || path == "/") {
    status->exists = true;
    status->is_directory = true;
    return Status::OK();
  }
  auto it = files_.find(path);
  if (it != files_.end()) {
    *status = it->second;
    return Status::OK();
  }
  return Status::NotFound();
}

Status FakeUfs::List(const std::string& path,
                     std::vector<FileStatus>* entries) {
  if (!entries) return Status::InvalidArgument(nullptr);
  entries->clear();
  std::string prefix = path.empty() || path == "/" ? "" : path + "/";
  for (const auto& [p, fs] : files_) {
    if (prefix.empty()) {
      if (p.find('/') == std::string::npos) {
        entries->push_back(fs);
      }
    } else if (p.size() > prefix.size() && p.compare(0, prefix.size(), prefix) == 0) {
      std::string rel = p.substr(prefix.size());
      size_t slash = rel.find('/');
      std::string name = slash == std::string::npos ? rel : rel.substr(0, slash);
      bool found = false;
      for (const auto& e : *entries) {
        if (e.path == name) { found = true; break; }
      }
      if (!found) {
        FileStatus e;
        e.exists = true;
        e.is_directory = slash != std::string::npos;
        e.size = fs.size;
        e.mtime_ms = fs.mtime_ms;
        e.path = name;
        entries->push_back(std::move(e));
      }
    }
  }
  return Status::OK();
}

Status FakeUfs::Delete(const std::string& path) {
  (void)path;
  return Status::InvalidArgument("FakeUfs::Delete not implemented");
}

Status FakeUfs::Rename(const std::string& src, const std::string& dst) {
  (void)src;
  (void)dst;
  return Status::InvalidArgument("FakeUfs::Rename not implemented");
}

Status FakeUfs::Mkdirs(const std::string& path) {
  (void)path;
  return Status::InvalidArgument("FakeUfs::Mkdirs not implemented");
}

}  // namespace fluxcache
