#pragma once

#include "ufs/ufs.h"

#include <cstdint>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace fluxcache {

// Test-only UFS stub with configurable mtime/size for GetFileInfo verification.
// Register via ufs_factory.h RegisterFakeUfsForTest, then use "fake://<authority>" as ufs_uri.
class FakeUfs : public UFS {
 public:
  // Add a file at path with given size and mtime_ms. Path is relative (e.g. "file.txt").
  void AddFile(const std::string& path, uint64_t size, int64_t mtime_ms);

  std::unique_ptr<UFS> Clone() const override;

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
  std::map<std::string, FileStatus> files_;
};

}  // namespace fluxcache
