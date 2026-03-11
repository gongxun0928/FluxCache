#pragma once

#include "ufs/ufs.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace fluxcache {

// Test-only UFS stub with configurable mtime/size for GetFileInfo verification.
// Register via ufs_factory.h RegisterFakeUfsForTest, then use "fake://<authority>" as ufs_uri.
// AddFileWithContent + read_count support ReadPages cache hit/miss verification.
class FakeUfs : public UFS {
 public:
  // Add a file at path with given size and mtime_ms. Path is relative (e.g. "file.txt").
  void AddFile(const std::string& path, uint64_t size, int64_t mtime_ms);

  // Add a file with content for Read() to return. Size = content.size().
  void AddFileWithContent(const std::string& path, const std::string& content,
                          int64_t mtime_ms);

  // Number of Read() calls. Shared across clones for cache hit/miss verification.
  int64_t read_count() const { return read_count_ ? read_count_->load() : 0; }

  // Test-only: when true, Write() returns error. Used for atomic boundary verification.
  void SetWriteFail(bool fail) { write_fail_ = fail; }
  bool write_fail() const { return write_fail_; }

  // Test-only: when true, Read() returns error. Used for degradation/fault injection.
  void SetReadFail(bool fail) { read_fail_->store(fail); }
  bool read_fail() const { return read_fail_->load(); }

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
  using FilesMap = std::map<std::string, FileStatus>;
  using ContentMap = std::map<std::string, std::string>;
  std::shared_ptr<FilesMap> files_{std::make_shared<FilesMap>()};
  std::shared_ptr<ContentMap> content_{std::make_shared<ContentMap>()};
  std::shared_ptr<std::atomic<int64_t>> read_count_{
      std::make_shared<std::atomic<int64_t>>(0)};
  bool write_fail_{false};
  std::shared_ptr<std::atomic<bool>> read_fail_{
      std::make_shared<std::atomic<bool>>(false)};
};

}  // namespace fluxcache
