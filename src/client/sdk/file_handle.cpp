#include "client/sdk/file_handle.h"
#include "client/fluxcache_client.h"
#include "common/config/config.h"
#include <cstring>
#include <string>

namespace fluxcache {

struct FileHandle::Impl {
  std::string path;
  FluxCacheClient* client = nullptr;
  FileInfo info;
  bool closed = false;
};

FileHandle::FileHandle(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

FileHandle::~FileHandle() {
  if (impl_ && !impl_->closed) {
    impl_->closed = true;
  }
}

StatusOr<size_t> FileHandle::Read(void* buf, uint64_t offset, size_t size) {
  if (!impl_) {
    return Status::InvalidArgument("handle closed");
  }
  if (impl_->closed) {
    return Status::InvalidArgument("handle already closed");
  }
  auto result = impl_->client->Read(impl_->path, offset, size);
  if (!result.ok()) {
    return result.status();
  }
  const std::string& data = result.value();
  size_t to_copy = std::min(data.size(), size);
  if (to_copy > 0 && buf) {
    std::memcpy(buf, data.data(), to_copy);
  }
  return to_copy;
}

Status FileHandle::Write(uint64_t offset, std::string_view data) {
  if (!impl_) {
    return Status::InvalidArgument("handle closed");
  }
  if (impl_->closed) {
    return Status::InvalidArgument("handle already closed");
  }
  return impl_->client->Write(impl_->path, offset, data);
}

StatusOr<FileInfo> FileHandle::Stat() {
  if (!impl_) {
    return Status::InvalidArgument("handle closed");
  }
  if (impl_->closed) {
    return Status::InvalidArgument("handle already closed");
  }
  auto result = impl_->client->GetMasterClient()->GetFileInfo(impl_->path);
  if (!result.ok()) {
    return result.status();
  }
  const auto& fi = result.value().file_info();
  FileInfo info;
  info.inode_id = fi.inode_id();
  info.size = fi.size();
  info.is_directory = fi.is_directory();
  info.ufs_mtime_ms = fi.ufs_mtime_ms();
  return info;
}

Status FileHandle::Close() {
  if (!impl_) {
    return Status::OK();
  }
  impl_->closed = true;
  return Status::OK();
}

std::unique_ptr<FileHandle> FileHandle::Create(const std::string& path,
                                                 FluxCacheClient* client,
                                                 const FileInfo& info) {
  auto impl = std::make_unique<Impl>();
  impl->path = path;
  impl->client = client;
  impl->info = info;
  return std::unique_ptr<FileHandle>(new FileHandle(std::move(impl)));
}

}  // namespace fluxcache
