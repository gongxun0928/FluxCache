#pragma once

#include "client/sdk/file_handle.h"
#include "client/sdk/types.h"
#include "common/status.h"
#include "common/status_or.h"
#include <memory>
#include <string>

namespace fluxcache {

/// FluxCache C++ SDK for application developers.
///
/// MVP supports file-level operations: Create, Open, Read, Write, Stat, Delete.
/// Errors are returned via Status/StatusOr, no exceptions.
///
/// NOT supported in MVP (namespace APIs, to be added later):
/// - Rename
/// - List
/// - Mkdir
/// - Rmdir
/// - Exists
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

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  explicit FluxCacheSDK(std::unique_ptr<Impl> impl);
};

}  // namespace fluxcache
