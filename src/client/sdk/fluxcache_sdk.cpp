#include "client/sdk/fluxcache_sdk.h"
#include "client/sdk/file_handle.h"
#include "client/fluxcache_client.h"
#include "common/config/config.h"
#include <memory>

namespace fluxcache {

struct FluxCacheSDK::Impl {
  ClientConfig client_config;
  std::unique_ptr<FluxCacheClient> client;
};

StatusOr<std::unique_ptr<FluxCacheSDK>> FluxCacheSDK::Create(
    const SDKConfig& config) {
  if (config.master_host.empty()) {
    return Status::InvalidArgument("master_host required");
  }
  if (config.master_port == 0) {
    return Status::InvalidArgument("master_port required");
  }

  ClientConfig cc;
  cc.master_host = config.master_host;
  cc.master_port = config.master_port;
  cc.page_size = config.page_size > 0 ? config.page_size : 1024 * 1024;
  cc.channel_pool_size =
      config.channel_pool_size > 0 ? config.channel_pool_size : 4;
  cc.retry_max_attempts =
      config.retry_max_attempts > 0 ? config.retry_max_attempts : 3;
  cc.retry_initial_delay_ms = config.retry_initial_delay_ms > 0
                                  ? config.retry_initial_delay_ms
                                  : 50;
  cc.local_cache_enabled = config.local_cache_enabled;
  cc.local_cache_size_bytes =
      config.local_cache_size_mb > 0
          ? config.local_cache_size_mb * 1024 * 1024
          : 0;

  auto impl = std::make_unique<Impl>();
  impl->client_config = cc;
  impl->client = std::make_unique<FluxCacheClient>(cc);

  return std::unique_ptr<FluxCacheSDK>(
      new FluxCacheSDK(std::move(impl)));
}

FluxCacheSDK::FluxCacheSDK(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

FluxCacheSDK::~FluxCacheSDK() = default;

Status FluxCacheSDK::Create(const std::string& path) {
  auto result = impl_->client->GetMasterClient()->CreateFile(path);
  if (!result.ok()) {
    return result.status();
  }
  return Status::OK();
}

StatusOr<std::unique_ptr<FileHandle>> FluxCacheSDK::Open(
    const std::string& path, OpenMode /*mode*/) {
  auto result = impl_->client->GetMasterClient()->GetFileInfo(path);
  if (!result.ok()) {
    return result.status();
  }
  const auto& fi = result.value().file_info();
  if (fi.is_directory()) {
    return Status::InvalidArgument("cannot open directory as file");
  }

  FileInfo info;
  info.inode_id = fi.inode_id();
  info.size = fi.size();
  info.is_directory = fi.is_directory();
  info.ufs_mtime_ms = fi.ufs_mtime_ms();

  return FileHandle::Create(path, impl_->client.get(), info);
}

Status FluxCacheSDK::Delete(const std::string& path) {
  return impl_->client->Delete(path);
}

StatusOr<FileInfo> FluxCacheSDK::Stat(const std::string& path) {
  auto result = impl_->client->GetMasterClient()->GetFileInfo(path);
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

}  // namespace fluxcache
