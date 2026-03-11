#pragma once

#include "client/sdk/types.h"
#include "common/status.h"
#include "common/status_or.h"
#include <cstddef>
#include <memory>
#include <string_view>

namespace fluxcache {

class FluxCacheClient;


/// File handle for read/write operations. Not thread-safe per handle.
/// PIMPL: no gRPC/protobuf in this header.
class FileHandle {
 public:
  ~FileHandle();

  FileHandle(FileHandle&&) noexcept = default;
  FileHandle& operator=(FileHandle&&) noexcept = default;
  FileHandle(const FileHandle&) = delete;
  FileHandle& operator=(const FileHandle&) = delete;

  /// Read up to size bytes at offset into buf. Returns bytes read.
  StatusOr<size_t> Read(void* buf, uint64_t offset, size_t size);

  /// Write data at offset.
  Status Write(uint64_t offset, std::string_view data);

  /// Get current file metadata.
  StatusOr<FileInfo> Stat();

  /// Close handle. Idempotent.
  Status Close();

  /// Create FileHandle for path. Internal use by FluxCacheSDK.
  static std::unique_ptr<FileHandle> Create(const std::string& path,
                                            FluxCacheClient* client,
                                            const FileInfo& info);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  explicit FileHandle(std::unique_ptr<Impl> impl);
};

}  // namespace fluxcache
