#include "client/fuse/fuse_ops.h"

#include <fcntl.h>

#include <cstring>
#include <memory>
#include <string>

#include "client/sdk/fluxcache_sdk.h"
#include "client/sdk/types.h"
#include "common/status.h"

namespace fluxcache {

namespace {

std::string ToSdkPath(const FuseContext* ctx, const char* fuse_path) {
  if (!ctx || !fuse_path) return {};
  std::string p(fuse_path);
  if (p == "/") {
    return ctx->root.empty() ? "/" : ctx->root;
  }
  std::string r = ctx->root;
  if (r.empty() || r == "/") {
    return p;
  }
  if (r.back() == '/') r.pop_back();
  return r + p;
}

int ToErrno(StatusCode c) {
  switch (c) {
    case StatusCode::kOk:
      return 0;
    case StatusCode::kNotFound:
      return -ENOENT;
    case StatusCode::kAlreadyExists:
      return -EEXIST;
    case StatusCode::kDirectoryNotEmpty:
      return -ENOTEMPTY;
    case StatusCode::kInvalidArgument:
      return -EINVAL;
    case StatusCode::kIOError:
    case StatusCode::kUnavailable:
    case StatusCode::kResourceExhausted:
    default:
      return -EIO;
  }
}

FuseContext* GetContext() {
  struct fuse_context* ctx = fuse_get_context();
  return ctx ? static_cast<FuseContext*>(ctx->private_data) : nullptr;
}

int fc_getattr(const char* path, struct stat* stbuf,
               struct fuse_file_info* fi) {
  (void)fi;
  FuseContext* ctx = GetContext();
  if (!ctx || !ctx->sdk || !stbuf) return -EIO;

  std::string sdk_path = ToSdkPath(ctx, path);
  if (sdk_path.empty()) return -EINVAL;

  auto result = ctx->sdk->Stat(sdk_path);
  if (!result.ok()) {
    return ToErrno(result.status().code());
  }

  std::memset(stbuf, 0, sizeof(struct stat));
  const FileInfo& info = result.value();
  if (info.is_directory) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
  } else {
    stbuf->st_mode = S_IFREG | 0644;
    stbuf->st_nlink = 1;
    stbuf->st_size = static_cast<off_t>(info.size);
  }
  stbuf->st_ino = static_cast<ino_t>(info.inode_id);
  // file_version used for cache validation; st_mtime left as 0
  return 0;
}

int fc_open(const char* path, struct fuse_file_info* fi) {
  if (!fi) return -EINVAL;
  FuseContext* ctx = GetContext();
  if (!ctx || !ctx->sdk) return -EIO;

  std::string sdk_path = ToSdkPath(ctx, path);
  if (sdk_path.empty()) return -EINVAL;

  OpenMode mode = OpenMode::kReadOnly;
  int flags = fi->flags & O_ACCMODE;
  if (flags == O_WRONLY) {
    mode = OpenMode::kWriteOnly;
  } else if (flags == O_RDWR) {
    mode = OpenMode::kReadWrite;
  }

  auto result = ctx->sdk->Open(sdk_path, mode);
  if (!result.ok()) {
    return ToErrno(result.status().code());
  }

  fi->fh = reinterpret_cast<uint64_t>(result.value().release());
  return 0;
}

int fc_release(const char* path, struct fuse_file_info* fi) {
  (void)path;
  if (!fi || !fi->fh) return 0;
  auto* handle = reinterpret_cast<FileHandle*>(fi->fh);
  handle->Close();
  delete handle;
  fi->fh = 0;
  return 0;
}

int fc_read(const char* path, char* buf, size_t size, off_t offset,
            struct fuse_file_info* fi) {
  (void)path;
  if (!fi || !fi->fh || !buf) return -EINVAL;
  auto* handle = reinterpret_cast<FileHandle*>(fi->fh);

  auto result = handle->Read(buf, static_cast<uint64_t>(offset), size);
  if (!result.ok()) {
    return ToErrno(result.status().code());
  }
  return static_cast<int>(result.value());
}

int fc_write(const char* path, const char* buf, size_t size, off_t offset,
             struct fuse_file_info* fi) {
  (void)path;
  if (!fi || !fi->fh || !buf) return -EINVAL;
  auto* handle = reinterpret_cast<FileHandle*>(fi->fh);

  std::string_view data(buf, size);
  auto status = handle->Write(static_cast<uint64_t>(offset), data);
  if (!status.ok()) {
    return ToErrno(status.code());
  }
  return static_cast<int>(size);
}

int fc_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
  (void)mode;
  FuseContext* ctx = GetContext();
  if (!ctx || !ctx->sdk || !fi) return -EIO;

  std::string sdk_path = ToSdkPath(ctx, path);
  if (sdk_path.empty()) return -EINVAL;

  auto create_status = ctx->sdk->Create(sdk_path);
  if (!create_status.ok()) {
    if (create_status.code() == StatusCode::kAlreadyExists) {
      return -EEXIST;
    }
    return ToErrno(create_status.code());
  }

  auto open_result = ctx->sdk->Open(sdk_path, OpenMode::kReadWrite);
  if (!open_result.ok()) {
    return ToErrno(open_result.status().code());
  }

  fi->fh = reinterpret_cast<uint64_t>(open_result.value().release());
  return 0;
}

int fc_rename(const char* oldpath, const char* newpath, unsigned int flags) {
  // RENAME_EXCHANGE is not supported (swap semantics).
  if (flags & RENAME_EXCHANGE) return -ENOTSUP;

  FuseContext* ctx = GetContext();
  if (!ctx || !ctx->sdk) return -EIO;

  std::string src = ToSdkPath(ctx, oldpath);
  std::string dst = ToSdkPath(ctx, newpath);
  if (src.empty() || dst.empty()) return -EINVAL;

  // RENAME_NOREPLACE: fail if dst already exists.
  if (flags & RENAME_NOREPLACE) {
    auto stat = ctx->sdk->Stat(dst);
    if (stat.ok()) return -EEXIST;
  }

  auto status = ctx->sdk->Rename(src, dst);
  return ToErrno(status.code());
}

int fc_unlink(const char* path) {
  FuseContext* ctx = GetContext();
  if (!ctx || !ctx->sdk) return -EIO;

  std::string sdk_path = ToSdkPath(ctx, path);
  if (sdk_path.empty()) return -EINVAL;

  auto status = ctx->sdk->Delete(sdk_path);
  return ToErrno(status.code());
}

int fc_mkdir(const char* path, mode_t /*mode*/) {
  FuseContext* ctx = GetContext();
  if (!ctx || !ctx->sdk) return -EIO;

  std::string sdk_path = ToSdkPath(ctx, path);
  if (sdk_path.empty()) return -EINVAL;

  auto status = ctx->sdk->Mkdir(sdk_path);
  return ToErrno(status.code());
}

int fc_rmdir(const char* path) {
  FuseContext* ctx = GetContext();
  if (!ctx || !ctx->sdk) return -EIO;

  std::string sdk_path = ToSdkPath(ctx, path);
  if (sdk_path.empty()) return -EINVAL;

  auto status = ctx->sdk->Rmdir(sdk_path);
  return ToErrno(status.code());
}

int fc_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
               off_t /*offset*/, struct fuse_file_info* /*fi*/,
               enum fuse_readdir_flags /*flags*/) {
  FuseContext* ctx = GetContext();
  if (!ctx || !ctx->sdk || !buf || !filler) return -EIO;

  std::string sdk_path = ToSdkPath(ctx, path);
  if (sdk_path.empty()) return -EINVAL;

  auto result = ctx->sdk->ListDirectory(sdk_path);
  if (!result.ok()) {
    return ToErrno(result.status().code());
  }

  // Always include . and ..
  filler(buf, ".", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
  filler(buf, "..", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));

  for (const auto& entry : result.value()) {
    filler(buf, entry.name.c_str(), nullptr, 0,
           static_cast<fuse_fill_dir_flags>(0));
  }

  return 0;
}

int fc_truncate(const char* path, off_t size, struct fuse_file_info* /*fi*/) {
  // Phase 1: truncate is not fully supported by the backend.
  // For size=0 on an existing file, we accept it as a no-op to make
  // common tools (cp, mv) happy.
  if (size == 0) {
    FuseContext* ctx = GetContext();
    if (!ctx || !ctx->sdk) return -EIO;
    std::string sdk_path = ToSdkPath(ctx, path);
    if (sdk_path.empty()) return -EINVAL;
    // Verify path exists.
    auto result = ctx->sdk->Stat(sdk_path);
    if (!result.ok()) return ToErrno(result.status().code());
    return 0;
  }
  return -ENOTSUP;
}

int fc_chmod(const char* /*path*/, mode_t /*mode*/,
             struct fuse_file_info* /*fi*/) {
  // Cache filesystem does not persist permission changes in Phase 1.
  return 0;  // Silently accept.
}

int fc_chown(const char* /*path*/, uid_t /*uid*/, gid_t /*gid*/,
             struct fuse_file_info* /*fi*/) {
  // Cache filesystem does not persist ownership changes in Phase 1.
  return 0;  // Silently accept.
}

int fc_utimens(const char* path, const struct timespec* /*ts*/,
               struct fuse_file_info* /*fi*/) {
  // Accept utimens as a no-op for compatibility. mtime updates are
  // handled internally by the cache system.
  (void)path;
  return 0;
}

}  // namespace

void RegisterFluxCacheFuseOps(struct fuse_operations* ops) {
  std::memset(ops, 0, sizeof(struct fuse_operations));
  ops->getattr = fc_getattr;
  ops->open = fc_open;
  ops->release = fc_release;
  ops->read = fc_read;
  ops->write = fc_write;
  ops->create = fc_create;
  ops->rename = fc_rename;
  ops->unlink = fc_unlink;
  ops->mkdir = fc_mkdir;
  ops->rmdir = fc_rmdir;
  ops->readdir = fc_readdir;
  ops->truncate = fc_truncate;
  ops->chmod = fc_chmod;
  ops->chown = fc_chown;
  ops->utimens = fc_utimens;
}

std::string FusePathToSdkPath(const FuseContext* ctx, const char* fuse_path) {
  return ToSdkPath(ctx, fuse_path);
}

int StatusToErrno(int status_code, int default_errno) {
  (void)default_errno;
  return ToErrno(static_cast<StatusCode>(status_code));
}

}  // namespace fluxcache
