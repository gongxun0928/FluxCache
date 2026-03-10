#pragma once

#include "ufs/ufs.h"

#include <string>

namespace fluxcache {

class LocalUFS : public UFS {
 public:
  explicit LocalUFS(std::string root_path);

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
  std::string ResolvePath(const std::string& path) const;
  bool IsPathUnderRoot(const std::string& resolved) const;

  std::string root_path_;
};

}  // namespace fluxcache
