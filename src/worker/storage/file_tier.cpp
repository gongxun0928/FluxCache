#include "worker/storage/file_tier.h"
#include "common/status.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace fluxcache {

namespace fs = std::filesystem;

FileTier::FileTier(std::string root_path, size_t capacity_limit)
    : root_path_(std::move(root_path)), capacity_limit_(capacity_limit) {
  if (!root_path_.empty() && root_path_.back() != '/') {
    root_path_ += '/';
  }
  EnsureRootExists();
  RecoverFromDisk();
}

std::string FileTier::BlockPath(uint64_t id) const {
  std::ostringstream oss;
  oss << root_path_ << "block-" << id << ".dat";
  return oss.str();
}

Status FileTier::EnsureRootExists() {
  std::error_code ec;
  fs::create_directories(root_path_, ec);
  return ec ? Status::IOError(ec.message().c_str()) : Status::OK();
}

Status FileTier::RecoverFromDisk() {
  std::error_code ec;
  if (!fs::exists(root_path_, ec) || !fs::is_directory(root_path_, ec)) {
    return Status::OK();
  }
  std::regex re(R"(block-(\d+)\.dat)");
  uint64_t max_id = 0;
  used_ = 0;
  block_sizes_.clear();
  for (const auto& entry : fs::directory_iterator(root_path_, ec)) {
    if (ec) return Status::IOError(ec.message().c_str());
    if (!entry.is_regular_file(ec)) continue;
    if (ec) continue;
    std::string name = entry.path().filename().string();
    std::smatch m;
    if (!std::regex_match(name, m, re)) continue;
    uint64_t id = 0;
    try {
      id = std::stoull(m[1].str());
    } catch (...) {
      continue;
    }
    auto sz = entry.file_size(ec);
    if (ec) continue;
    size_t size = static_cast<size_t>(sz);
    block_sizes_[id] = size;
    used_ += size;
    if (id >= max_id) max_id = id + 1;
  }
  next_id_ = (max_id == 0) ? 1 : max_id;
  return Status::OK();
}

Status FileTier::Allocate(size_t size, TierBlockHandle* handle) {
  if (size == 0) {
    return Status::InvalidArgument("allocate size must be positive");
  }
  if (used_ + size > capacity_limit_) {
    return Status::ResourceExhausted("file tier capacity exhausted");
  }
  uint64_t id = next_id_++;
  std::string path = BlockPath(id);
  std::ofstream f(path, std::ios::binary | std::ios::out | std::ios::trunc);
  if (!f) {
    return Status::IOError("failed to create block file");
  }
  std::string zeros(size, '\0');
  f.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
  if (!f) {
    fs::remove(path);
    return Status::IOError("failed to write block file");
  }
  used_ += size;
  block_sizes_[id] = size;
  handle->id = id;
  return Status::OK();
}

Status FileTier::Write(const TierBlockHandle& handle, size_t offset,
                       std::string_view data) {
  if (!handle.valid()) {
    return Status::InvalidArgument("invalid handle");
  }
  auto it = block_sizes_.find(handle.id);
  if (it == block_sizes_.end()) {
    return Status::InvalidArgument("handle not found");
  }
  if (offset + data.size() > it->second) {
    return Status::InvalidArgument("write exceeds block size");
  }
  std::string path = BlockPath(handle.id);
  std::ofstream f(path, std::ios::binary | std::ios::in | std::ios::out);
  if (!f) {
    f.open(path, std::ios::binary | std::ios::out);
    if (!f) return Status::IOError("failed to open block file for write");
  }
  f.seekp(static_cast<std::streamoff>(offset));
  if (!f) return Status::IOError("seek failed");
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!f) return Status::IOError("write failed");
  return Status::OK();
}

Status FileTier::Read(const TierBlockHandle& handle, size_t offset, size_t size,
                      std::string* out) {
  if (!out) {
    return Status::InvalidArgument("null output");
  }
  if (!handle.valid()) {
    return Status::InvalidArgument("invalid handle");
  }
  auto it = block_sizes_.find(handle.id);
  if (it == block_sizes_.end()) {
    return Status::InvalidArgument("handle not found");
  }
  if (offset + size > it->second) {
    return Status::InvalidArgument("read exceeds block size");
  }
  std::string path = BlockPath(handle.id);
  std::ifstream f(path, std::ios::binary);
  if (!f) return Status::IOError("failed to open block file for read");
  f.seekg(static_cast<std::streamoff>(offset));
  if (!f) return Status::IOError("seek failed");
  out->resize(size);
  f.read(out->data(), static_cast<std::streamsize>(size));
  if (!f && !f.eof()) return Status::IOError("read failed");
  size_t got = static_cast<size_t>(f.gcount());
  out->resize(got);
  return Status::OK();
}

Status FileTier::Release(const TierBlockHandle& handle) {
  if (!handle.valid()) {
    return Status::InvalidArgument("invalid handle");
  }
  auto it = block_sizes_.find(handle.id);
  if (it == block_sizes_.end()) {
    return Status::InvalidArgument("handle not found");
  }
  used_ -= it->second;
  block_sizes_.erase(it);
  std::string path = BlockPath(handle.id);
  std::error_code ec;
  fs::remove(path, ec);
  return Status::OK();
}

}  // namespace fluxcache
