#pragma once

#include "client/sdk/file_handle.h"
#include "client/sdk/types.h"
#include "common/status.h"
#include "common/status_or.h"
#include <memory>
#include <string>
#include <vector>

namespace fluxcache {

/// FluxCache C++ SDK for application developers.
///
/// Supports file-level and directory-level operations.
/// Errors are returned via Status/StatusOr, no exceptions.
///
/// PIMPL: no gRPC/protobuf in this header.
class FluxCacheSDK {
 public:
  /// Create SDK instance. Returns error if config invalid.
  static StatusOr<std::unique_ptr<FluxCacheSDK>> Create(const SDKConfig& config);

  ~FluxCacheSDK();

  FluxCacheSDK(FluxCacheSDK&&) noexcept = default;
  FluxCacheSDK& operator=(FluxCacheSDK&&) noexcept = default;
  FluxCacheSDK(const FluxCacheSDK&) = delete;
  FluxCacheSDK& operator=(const FluxCacheSDK&) = delete;

  /// Create new file at path. Returns AlreadyExists if path exists.
  Status Create(const std::string& path);

  /// Open file at path. Returns NotFound if path does not exist.
  StatusOr<std::unique_ptr<FileHandle>> Open(const std::string& path,
                                             OpenMode mode);

  /// Delete file at path. Returns NotFound if path does not exist.
  /// May return Unavailable if server DeleteFile not yet implemented.
  Status Delete(const std::string& path);

  /// Get file metadata. Returns NotFound if path does not exist.
  StatusOr<FileInfo> Stat(const std::string& path);

  /// Create directory at path. Returns AlreadyExists if path exists.
  Status Mkdir(const std::string& path);

  /// Remove empty directory at path. Returns NotFound if not found,
  /// FailedPrecondition if directory is not empty.
  Status Rmdir(const std::string& path);

  /// List directory contents. Returns vector of DirEntry.
  /// Returns NotFound if path does not exist or is not a directory.
  StatusOr<std::vector<DirEntry>> ListDirectory(const std::string& path);

  /// Rename src_path to dst_path. Overwrites dst if it exists and is a file
  /// or empty directory. Returns NotFound if src does not exist.
  Status Rename(const std::string& src_path, const std::string& dst_path);

  /// Check if path exists. Does not distinguish files from directories.
  bool Exists(const std::string& path);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  explicit FluxCacheSDK(std::unique_ptr<Impl> impl);
};

}  // namespace fluxcache
