#pragma once

#include "ufs/ufs.h"

#include <string>

namespace fluxcache {

/// S3-compatible UFS driver stub.
/// Real implementation requires AWS SDK for C++ (aws-cpp-sdk-s3).
/// All operations return Unavailable until the SDK is linked.
class S3UFS : public UFS {
 public:
  explicit S3UFS(std::string authority);

  Status Read(const std::string& path, uint64_t offset, uint64_t size,
              std::string* out) override;
  Status Write(const std::string& path, uint64_t offset,
               std::string_view data) override;
  Status GetStatus(const std::string& path, FileStatus* status) override;
  Status List(const std::string& path,
              std::vector<FileStatus>* entries) override;
  Status Delete(const std::string& path) override;
  Status Rename(const std::string& src, const std::string& dst) override;
  Status Mkdirs(const std::string& path) override;

 private:
  std::string authority_;
};

}  // namespace fluxcache
