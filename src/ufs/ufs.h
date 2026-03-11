#pragma once

#include "common/status.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fluxcache {

struct FileStatus {
  bool exists{false};
  bool is_directory{false};
  uint64_t size{0};
  int64_t mtime_ms{0};
  std::string path;
};

class UFS {
 public:
  virtual ~UFS() = default;

  // Returns a new instance with same state. Used by tests (e.g. FakeUfs).
  virtual std::unique_ptr<UFS> Clone() const { return nullptr; }

  virtual Status Read(const std::string& path, uint64_t offset, uint64_t size,
                      std::string* out) = 0;
  virtual Status Write(const std::string& path, uint64_t offset,
                       std::string_view data) = 0;
  virtual Status GetStatus(const std::string& path, FileStatus* status) = 0;
  virtual Status List(const std::string& path,
                      std::vector<FileStatus>* entries) = 0;
  virtual Status Delete(const std::string& path) = 0;
  virtual Status Rename(const std::string& src, const std::string& dst) = 0;
  virtual Status Mkdirs(const std::string& path) = 0;
};

}  // namespace fluxcache
