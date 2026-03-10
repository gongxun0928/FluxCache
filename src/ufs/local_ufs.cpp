#include "ufs/local_ufs.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fluxcache {

namespace {

namespace fs = std::filesystem;

int64_t GetMtimeMs(const fs::path& p) {
  auto ft = fs::last_write_time(p);
  auto sct = std::chrono::file_clock::to_sys(ft);
  return static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          sct.time_since_epoch())
          .count());
}

}  // namespace

LocalUFS::LocalUFS(std::string root_path) : root_path_(std::move(root_path)) {
  if (!root_path_.empty() && root_path_.back() != '/') {
    root_path_ += '/';
  }
}

std::string LocalUFS::ResolvePath(const std::string& path) const {
  fs::path base(root_path_);
  std::string path_rel = path;
  while (!path_rel.empty() && path_rel.front() == '/') path_rel.erase(0, 1);
  fs::path p = base / path_rel;
  try {
    return fs::weakly_canonical(p).string();
  } catch (...) {
    return (base / path_rel).string();
  }
}

bool LocalUFS::IsPathUnderRoot(const std::string& resolved) const {
  fs::path root_canonical;
  try {
    root_canonical = fs::weakly_canonical(root_path_);
  } catch (...) {
    root_canonical = fs::path(root_path_);
  }
  std::string root_s = root_canonical.string();
  if (!root_s.empty() && root_s.back() != '/') root_s += '/';
  return resolved == root_s ||
         (resolved.size() > root_s.size() &&
          resolved.compare(0, root_s.size(), root_s) == 0);
}

Status LocalUFS::Read(const std::string& path, uint64_t offset, uint64_t size,
                      std::string* out) {
  if (!out) return Status::InvalidArgument(nullptr);
  std::string resolved = ResolvePath(path);
  if (!IsPathUnderRoot(resolved)) return Status::InvalidArgument(nullptr);

  std::ifstream f(resolved, std::ios::binary);
  if (!f) return Status::NotFound();

  f.seekg(static_cast<std::streamoff>(offset));
  if (!f) return Status::IOError();

  out->resize(size);
  f.read(out->data(), static_cast<std::streamsize>(size));
  if (!f && !f.eof()) return Status::IOError();
  out->resize(static_cast<size_t>(f.gcount()));
  return Status::OK();
}

Status LocalUFS::Write(const std::string& path, uint64_t offset,
                       std::string_view data) {
  std::string resolved = ResolvePath(path);
  if (!IsPathUnderRoot(resolved)) return Status::InvalidArgument(nullptr);

  fs::path logical_path(path);
  if (logical_path.has_parent_path()) {
    Status st = Mkdirs(logical_path.parent_path().string());
    if (!st.ok()) return st;
  }

  std::ofstream f;
  if (offset == 0) {
    f.open(resolved, std::ios::binary | std::ios::out | std::ios::trunc);
  } else {
    f.open(resolved, std::ios::binary | std::ios::in | std::ios::out);
    if (!f) {
      f.open(resolved, std::ios::binary | std::ios::out);
      if (!f) return Status::IOError();
    }
  }

  if (offset > 0) {
    f.seekp(static_cast<std::streamoff>(offset));
    if (!f) return Status::IOError();
  }
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!f) return Status::IOError();
  return Status::OK();
}

Status LocalUFS::GetStatus(const std::string& path, FileStatus* status) {
  if (!status) return Status::InvalidArgument(nullptr);
  *status = FileStatus{};
  status->path = path;

  std::string resolved = ResolvePath(path);
  if (!IsPathUnderRoot(resolved)) return Status::InvalidArgument(nullptr);

  std::error_code ec;
  auto st = fs::status(resolved, ec);
  if (ec || !fs::exists(st)) {
    return Status::OK();
  }

  status->exists = true;
  status->is_directory = fs::is_directory(st);
  if (status->is_directory) {
    status->size = 0;
  } else {
    auto sz = fs::file_size(resolved, ec);
    status->size = ec ? 0 : static_cast<uint64_t>(sz);
  }
  status->mtime_ms = GetMtimeMs(resolved);
  return Status::OK();
}

Status LocalUFS::List(const std::string& path,
                     std::vector<FileStatus>* entries) {
  if (!entries) return Status::InvalidArgument(nullptr);
  entries->clear();

  std::string resolved = ResolvePath(path);
  if (!IsPathUnderRoot(resolved)) return Status::InvalidArgument(nullptr);

  std::error_code ec;
  if (!fs::exists(resolved, ec) || !fs::is_directory(resolved, ec)) {
    return Status::NotFound();
  }

  for (const auto& entry : fs::directory_iterator(resolved, ec)) {
    if (ec) return Status::IOError();
    FileStatus fs;
    fs.exists = true;
    fs.is_directory = entry.is_directory(ec);
    if (ec) continue;
    fs.path = entry.path().filename().string();
    if (!fs.is_directory) {
      auto sz = entry.file_size(ec);
      fs.size = ec ? 0 : static_cast<uint64_t>(sz);
    }
    fs.mtime_ms = GetMtimeMs(entry.path());
    entries->push_back(std::move(fs));
  }
  if (ec) return Status::IOError();
  return Status::OK();
}

Status LocalUFS::Delete(const std::string& path) {
  std::string resolved = ResolvePath(path);
  if (!IsPathUnderRoot(resolved)) return Status::InvalidArgument(nullptr);

  std::error_code ec;
  if (!fs::exists(resolved, ec)) return Status::NotFound();
  fs::remove_all(resolved, ec);
  return ec ? Status::IOError() : Status::OK();
}

Status LocalUFS::Rename(const std::string& src, const std::string& dst) {
  std::string src_resolved = ResolvePath(src);
  std::string dst_resolved = ResolvePath(dst);
  if (!IsPathUnderRoot(src_resolved) || !IsPathUnderRoot(dst_resolved)) {
    return Status::InvalidArgument(nullptr);
  }

  std::error_code ec;
  if (!fs::exists(src_resolved, ec)) return Status::NotFound();
  fs::rename(src_resolved, dst_resolved, ec);
  return ec ? Status::IOError() : Status::OK();
}

Status LocalUFS::Mkdirs(const std::string& path) {
  std::string resolved = ResolvePath(path);
  if (!IsPathUnderRoot(resolved)) return Status::InvalidArgument(nullptr);

  std::error_code ec;
  fs::create_directories(resolved, ec);
  return ec ? Status::IOError() : Status::OK();
}

}  // namespace fluxcache
