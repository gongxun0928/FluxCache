#include "ufs/hdfs_ufs.h"

#include "common/status.h"

namespace fluxcache {

namespace {

const char* kNotImplementedMsg =
    "HDFS UFS requires libhdfs (Hadoop native client). "
    "Build with -DFLUXCACHE_USE_HDFS=ON and link libhdfs.";

}  // namespace

HdfsUFS::HdfsUFS(std::string authority) : authority_(std::move(authority)) {}

Status HdfsUFS::Read(const std::string& path, uint64_t offset, uint64_t size,
                     std::string* out) {
  (void)path;
  (void)offset;
  (void)size;
  (void)out;
  return Status::Unavailable(kNotImplementedMsg);
}

Status HdfsUFS::Write(const std::string& path, uint64_t offset,
                      std::string_view data) {
  (void)path;
  (void)offset;
  (void)data;
  return Status::Unavailable(kNotImplementedMsg);
}

Status HdfsUFS::GetStatus(const std::string& path, FileStatus* status) {
  (void)path;
  (void)status;
  return Status::Unavailable(kNotImplementedMsg);
}

Status HdfsUFS::List(const std::string& path,
                     std::vector<FileStatus>* entries) {
  (void)path;
  (void)entries;
  return Status::Unavailable(kNotImplementedMsg);
}

Status HdfsUFS::Delete(const std::string& path) {
  (void)path;
  return Status::Unavailable(kNotImplementedMsg);
}

Status HdfsUFS::Rename(const std::string& src, const std::string& dst) {
  (void)src;
  (void)dst;
  return Status::Unavailable(kNotImplementedMsg);
}

Status HdfsUFS::Mkdirs(const std::string& path) {
  (void)path;
  return Status::Unavailable(kNotImplementedMsg);
}

}  // namespace fluxcache
