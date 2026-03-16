#include "ufs/s3_ufs.h"

#include "common/status.h"

#include <miniocpp/client.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>

namespace fluxcache {

namespace {

// Parse authority: "bucket" or "host:port/bucket"
void ParseAuthority(const std::string& authority, std::string* endpoint,
                    std::string* bucket) {
  size_t slash = authority.find('/');
  if (slash == std::string::npos) {
    *endpoint = "";
    *bucket = authority;
    return;
  }
  *endpoint = authority.substr(0, slash);
  *bucket = authority.substr(slash + 1);
}

std::string ToObjectKey(const std::string& path) {
  std::string key = path;
  while (!key.empty() && key.front() == '/') key.erase(0, 1);
  return key;
}

bool GetS3Credentials(std::string* access_key, std::string* secret_key) {
  const char* ak = std::getenv("AWS_ACCESS_KEY_ID");
  const char* mu = std::getenv("MINIO_ROOT_USER");
  const char* sk = std::getenv("AWS_SECRET_ACCESS_KEY");
  const char* mp = std::getenv("MINIO_ROOT_PASSWORD");
  *access_key = ak ? ak : (mu ? mu : "");
  *secret_key = sk ? sk : (mp ? mp : "");
  return !access_key->empty() && !secret_key->empty();
}

}  // namespace

S3UFS::S3UFS(std::string authority) : authority_(std::move(authority)) {}

Status S3UFS::Read(const std::string& path, uint64_t offset, uint64_t size,
                   std::string* out) {
  if (!out) return Status::InvalidArgument(nullptr);

  std::string endpoint, bucket;
  ParseAuthority(authority_, &endpoint, &bucket);
  if (bucket.empty()) return Status::InvalidArgument("S3: empty bucket");

  std::string access_key, secret_key;
  if (!GetS3Credentials(&access_key, &secret_key)) {
    return Status::Unavailable(
        "S3: set AWS_ACCESS_KEY_ID/AWS_SECRET_ACCESS_KEY or "
        "MINIO_ROOT_USER/MINIO_ROOT_PASSWORD");
  }

  std::string object_key = ToObjectKey(path);
  if (object_key.empty()) return Status::InvalidArgument("S3: empty object key");

  std::string host =
      endpoint.empty() ? "s3.amazonaws.com" : endpoint;
  bool use_https = endpoint.empty();  // MinIO local typically http
  minio::s3::BaseUrl base_url(host, use_https);
  minio::creds::StaticProvider provider(access_key, secret_key);
  minio::s3::Client client(base_url, &provider);

  out->clear();
  out->reserve(size);
  size_t offset_val = offset;
  size_t length_val = size;

  minio::s3::GetObjectArgs args;
  args.bucket = bucket;
  args.object = object_key;
  args.offset = &offset_val;
  args.length = &length_val;
  args.datafunc = [out](minio::http::DataFunctionArgs fargs) -> bool {
    out->append(fargs.datachunk);
    return true;
  };

  minio::s3::GetObjectResponse resp = client.GetObject(args);
  if (!resp) {
    std::string err = resp.Error().String();
    if (err.find("404") != std::string::npos ||
        err.find("NoSuchKey") != std::string::npos) {
      return Status::NotFound(err.c_str());
    }
    if (err.find("403") != std::string::npos ||
        err.find("AccessDenied") != std::string::npos) {
      return Status::Unavailable(err.c_str());
    }
    return Status::IOError(err.c_str());
  }
  return Status::OK();
}

Status S3UFS::Write(const std::string& path, uint64_t offset,
                    std::string_view data) {
  std::string endpoint, bucket;
  ParseAuthority(authority_, &endpoint, &bucket);
  if (bucket.empty()) return Status::InvalidArgument("S3: empty bucket");

  std::string access_key, secret_key;
  if (!GetS3Credentials(&access_key, &secret_key)) {
    return Status::Unavailable(
        "S3: set AWS_ACCESS_KEY_ID/AWS_SECRET_ACCESS_KEY or "
        "MINIO_ROOT_USER/MINIO_ROOT_PASSWORD");
  }

  std::string object_key = ToObjectKey(path);
  if (object_key.empty()) return Status::InvalidArgument("S3: empty object key");

  std::string host = endpoint.empty() ? "s3.amazonaws.com" : endpoint;
  bool use_https = endpoint.empty();
  minio::s3::BaseUrl base_url(host, use_https);
  minio::creds::StaticProvider provider(access_key, secret_key);
  minio::s3::Client client(base_url, &provider);

  if (offset == 0) {
    minio::s3::PutObjectApiArgs args;
    args.bucket = bucket;
    args.object = object_key;
    args.data = std::string_view(data.data(), data.size());
    args.object_size = static_cast<long>(data.size());
    minio::s3::PutObjectResponse resp =
        static_cast<minio::s3::BaseClient&>(client).PutObject(args);
    if (!resp) {
      std::string err = resp.Error().String();
      if (err.find("403") != std::string::npos) {
        return Status::Unavailable(err.c_str());
      }
      return Status::IOError(err.c_str());
    }
    return Status::OK();
  }

  // offset > 0: read-modify-write (simplified; large files may need multipart)
  std::string existing;
  Status st = Read(path, 0, offset + data.size(), &existing);
  if (!st.ok() && st.code() != StatusCode::kNotFound) return st;
  if (st.code() == StatusCode::kNotFound) existing.resize(offset + data.size(), '\0');
  if (existing.size() < offset + data.size()) {
    existing.resize(offset + data.size(), '\0');
  }
  std::memcpy(existing.data() + offset, data.data(), data.size());
  return Write(path, 0, existing);
}

Status S3UFS::GetStatus(const std::string& path, FileStatus* status) {
  if (!status) return Status::InvalidArgument(nullptr);
  *status = FileStatus{};
  status->path = path;

  std::string endpoint, bucket;
  ParseAuthority(authority_, &endpoint, &bucket);
  if (bucket.empty()) return Status::InvalidArgument("S3: empty bucket");

  std::string access_key, secret_key;
  if (!GetS3Credentials(&access_key, &secret_key)) {
    return Status::Unavailable(
        "S3: set AWS_ACCESS_KEY_ID/AWS_SECRET_ACCESS_KEY or "
        "MINIO_ROOT_USER/MINIO_ROOT_PASSWORD");
  }

  std::string object_key = ToObjectKey(path);
  if (object_key.empty()) {
    status->exists = false;
    return Status::OK();
  }

  std::string host = endpoint.empty() ? "s3.amazonaws.com" : endpoint;
  bool use_https = endpoint.empty();
  minio::s3::BaseUrl base_url(host, use_https);
  minio::creds::StaticProvider provider(access_key, secret_key);
  minio::s3::Client client(base_url, &provider);

  minio::s3::StatObjectArgs args;
  args.bucket = bucket;
  args.object = object_key;
  minio::s3::StatObjectResponse resp = client.StatObject(args);
  if (!resp) {
    std::string err = resp.Error().String();
    if (err.find("404") != std::string::npos ||
        err.find("NoSuchKey") != std::string::npos) {
      status->exists = false;
      return Status::OK();
    }
    return Status::IOError(err.c_str());
  }
  status->exists = true;
  status->is_directory = false;
  status->size = resp.size;
  status->mtime_ms = 0;  // UtcTime does not expose Unix ms; use 0 for now
  return Status::OK();
}

Status S3UFS::List(const std::string& path,
                   std::vector<FileStatus>* entries) {
  if (!entries) return Status::InvalidArgument(nullptr);
  entries->clear();

  std::string endpoint, bucket;
  ParseAuthority(authority_, &endpoint, &bucket);
  if (bucket.empty()) return Status::InvalidArgument("S3: empty bucket");

  std::string access_key, secret_key;
  if (!GetS3Credentials(&access_key, &secret_key)) {
    return Status::Unavailable(
        "S3: set AWS_ACCESS_KEY_ID/AWS_SECRET_ACCESS_KEY or "
        "MINIO_ROOT_USER/MINIO_ROOT_PASSWORD");
  }

  std::string prefix = ToObjectKey(path);
  if (!prefix.empty() && prefix.back() != '/') prefix += '/';

  std::string host = endpoint.empty() ? "s3.amazonaws.com" : endpoint;
  bool use_https = endpoint.empty();
  minio::s3::BaseUrl base_url(host, use_https);
  minio::creds::StaticProvider provider(access_key, secret_key);
  minio::s3::Client client(base_url, &provider);

  minio::s3::ListObjectsArgs args;
  args.bucket = bucket;
  args.prefix = prefix;
  args.recursive = false;
  minio::s3::ListObjectsResult result = client.ListObjects(args);
  for (; result; result++) {
    minio::s3::Item item = *result;
    if (!item) {
      return Status::IOError(item.Error().String().c_str());
    }
    FileStatus fs;
    fs.exists = true;
    fs.is_directory = item.is_prefix;
    fs.size = item.size;
    fs.mtime_ms = 0;  // UtcTime does not expose Unix ms
    fs.path = item.name;
    if (fs.path.size() > prefix.size()) {
      fs.path = fs.path.substr(prefix.size());
    }
    size_t slash = fs.path.find('/');
    if (slash != std::string::npos) {
      fs.path = fs.path.substr(0, slash);
    }
    if (!fs.path.empty()) {
      entries->push_back(std::move(fs));
    }
  }
  return Status::OK();
}

Status S3UFS::Delete(const std::string& path) {
  std::string endpoint, bucket;
  ParseAuthority(authority_, &endpoint, &bucket);
  if (bucket.empty()) return Status::InvalidArgument("S3: empty bucket");

  std::string access_key, secret_key;
  if (!GetS3Credentials(&access_key, &secret_key)) {
    return Status::Unavailable(
        "S3: set AWS_ACCESS_KEY_ID/AWS_SECRET_ACCESS_KEY or "
        "MINIO_ROOT_USER/MINIO_ROOT_PASSWORD");
  }

  std::string object_key = ToObjectKey(path);
  if (object_key.empty()) return Status::InvalidArgument("S3: empty object key");

  std::string host = endpoint.empty() ? "s3.amazonaws.com" : endpoint;
  bool use_https = endpoint.empty();
  minio::s3::BaseUrl base_url(host, use_https);
  minio::creds::StaticProvider provider(access_key, secret_key);
  minio::s3::Client client(base_url, &provider);

  minio::s3::RemoveObjectArgs args;
  args.bucket = bucket;
  args.object = object_key;
  minio::s3::RemoveObjectResponse resp = client.RemoveObject(args);
  if (!resp) {
    std::string err = resp.Error().String();
    if (err.find("404") != std::string::npos) return Status::NotFound(err.c_str());
    return Status::IOError(err.c_str());
  }
  return Status::OK();
}

Status S3UFS::Rename(const std::string& src, const std::string& dst) {
  std::string endpoint, bucket;
  ParseAuthority(authority_, &endpoint, &bucket);
  if (bucket.empty()) return Status::InvalidArgument("S3: empty bucket");

  std::string access_key, secret_key;
  if (!GetS3Credentials(&access_key, &secret_key)) {
    return Status::Unavailable(
        "S3: set AWS_ACCESS_KEY_ID/AWS_SECRET_ACCESS_KEY or "
        "MINIO_ROOT_USER/MINIO_ROOT_PASSWORD");
  }

  std::string src_key = ToObjectKey(src);
  std::string dst_key = ToObjectKey(dst);
  if (src_key.empty() || dst_key.empty()) {
    return Status::InvalidArgument("S3: empty object key");
  }

  std::string host = endpoint.empty() ? "s3.amazonaws.com" : endpoint;
  bool use_https = endpoint.empty();
  minio::s3::BaseUrl base_url(host, use_https);
  minio::creds::StaticProvider provider(access_key, secret_key);
  minio::s3::Client client(base_url, &provider);

  minio::s3::CopySource source;
  source.bucket = bucket;
  source.object = src_key;
  minio::s3::CopyObjectArgs args;
  args.bucket = bucket;
  args.object = dst_key;
  args.source = source;
  minio::s3::CopyObjectResponse resp = client.CopyObject(args);
  if (!resp) return Status::IOError(resp.Error().String().c_str());

  Status st = Delete(src);
  if (!st.ok()) return st;
  return Status::OK();
}

Status S3UFS::Mkdirs(const std::string& path) {
  (void)path;
  return Status::OK();
}

}  // namespace fluxcache
