#pragma once

#include "ufs/ufs.h"

#include <string>

namespace fluxcache {

/// HDFS-compatible UFS driver stub.
/// Real implementation requires libhdfs (Hadoop native client).
/// All operations return Unavailable until libhdfs is linked.
class HdfsUFS : public UFS {
 public:
  explicit HdfsUFS(std::string authority);

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
