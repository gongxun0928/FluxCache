#include "ufs/s3_ufs.h"

#include "common/status.h"

namespace fluxcache {

namespace {

const char* kNotImplementedMsg =
    "S3 UFS requires AWS SDK for C++ (aws-cpp-sdk-s3). "
    "Build with -DFLUXCACHE_USE_AWS_SDK=ON and link the SDK.";

}  // namespace

S3UFS::S3UFS(std::string authority) : authority_(std::move(authority)) {}

Status S3UFS::Read(const std::string& path, uint64_t offset, uint64_t size,
                   std::string* out) {
  (void)path;
  (void)offset;
  (void)size;
  (void)out;
  return Status::Unavailable(kNotImplementedMsg);
}

Status S3UFS::Write(const std::string& path, uint64_t offset,
                    std::string_view data) {
  (void)path;
  (void)offset;
  (void)data;
  return Status::Unavailable(kNotImplementedMsg);
}

Status S3UFS::GetStatus(const std::string& path, FileStatus* status) {
  (void)path;
  (void)status;
  return Status::Unavailable(kNotImplementedMsg);
}

Status S3UFS::List(const std::string& path,
                   std::vector<FileStatus>* entries) {
  (void)path;
  (void)entries;
  return Status::Unavailable(kNotImplementedMsg);
}

Status S3UFS::Delete(const std::string& path) {
  (void)path;
  return Status::Unavailable(kNotImplementedMsg);
}

Status S3UFS::Rename(const std::string& src, const std::string& dst) {
  (void)src;
  (void)dst;
  return Status::Unavailable(kNotImplementedMsg);
}

Status S3UFS::Mkdirs(const std::string& path) {
  (void)path;
  return Status::Unavailable(kNotImplementedMsg);
}

}  // namespace fluxcache
