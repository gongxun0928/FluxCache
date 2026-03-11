#include "client/fuse/fuse_ops.h"
#include "client/sdk/fluxcache_sdk.h"
#include "client/sdk/types.h"
#include "common/status.h"
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>

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

int fc_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
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
  if (info.ufs_mtime_ms != 0) {
    stbuf->st_mtime = info.ufs_mtime_ms / 1000;
  }
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

// Unsupported operations: return -ENOTSUP explicitly
int fc_rename(const char* /*oldpath*/, const char* /*newpath*/,
              unsigned int /*flags*/) {
  return -ENOTSUP;
}
int fc_link(const char* /*oldpath*/, const char* /*newpath*/) {
  return -ENOTSUP;
}
int fc_symlink(const char* /*target*/, const char* /*path*/) {
  return -ENOTSUP;
}
int fc_readlink(const char* /*path*/, char* /*buf*/, size_t /*size*/) {
  return -ENOTSUP;
}
int fc_mkdir(const char* /*path*/, mode_t /*mode*/) { return -ENOTSUP; }
int fc_rmdir(const char* /*path*/) { return -ENOTSUP; }
int fc_readdir(const char* /*path*/, void* /*buf*/, fuse_fill_dir_t /*filler*/,
               off_t /*offset*/, struct fuse_file_info* /*fi*/,
               enum fuse_readdir_flags /*flags*/) {
  return -ENOTSUP;
}
int fc_chmod(const char* /*path*/, mode_t /*mode*/,
             struct fuse_file_info* /*fi*/) {
  return -ENOTSUP;
}
int fc_chown(const char* /*path*/, uid_t /*uid*/, gid_t /*gid*/,
             struct fuse_file_info* /*fi*/) {
  return -ENOTSUP;
}
int fc_utimens(const char* /*path*/, const struct timespec* /*ts*/,
              struct fuse_file_info* /*fi*/) {
  return -ENOTSUP;
}
int fc_setxattr(const char* /*path*/, const char* /*name*/,
               const char* /*value*/, size_t /*size*/, int /*flags*/) {
  return -ENOTSUP;
}
int fc_getxattr(const char* /*path*/, const char* /*name*/, char* /*value*/,
                size_t /*size*/) {
  return -ENOTSUP;
}
int fc_listxattr(const char* /*path*/, char* /*list*/, size_t /*size*/) {
  return -ENOTSUP;
}
int fc_removexattr(const char* /*path*/, const char* /*name*/) {
  return -ENOTSUP;
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
  ops->link = fc_link;
  ops->symlink = fc_symlink;
  ops->readlink = fc_readlink;
  ops->mkdir = fc_mkdir;
  ops->rmdir = fc_rmdir;
  ops->readdir = fc_readdir;
  ops->chmod = fc_chmod;
  ops->chown = fc_chown;
  ops->utimens = fc_utimens;
  ops->setxattr = fc_setxattr;
  ops->getxattr = fc_getxattr;
  ops->listxattr = fc_listxattr;
  ops->removexattr = fc_removexattr;
}

std::string FusePathToSdkPath(const FuseContext* ctx, const char* fuse_path) {
  return ToSdkPath(ctx, fuse_path);
}

int StatusToErrno(int status_code, int default_errno) {
  (void)default_errno;
  return ToErrno(static_cast<StatusCode>(status_code));
}

}  // namespace fluxcache
