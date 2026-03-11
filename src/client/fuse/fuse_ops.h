#pragma once

// Must define before including fuse headers
#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <cerrno>
#include <cstddef>
#include <string>

namespace fluxcache {

class FluxCacheSDK;

struct FuseContext {
  std::string root;
  FluxCacheSDK* sdk = nullptr;
};

/// Register FUSE operations into the given fuse_operations struct.
/// The context (SDK + root path) is passed via fuse_get_context()->private_data.
void RegisterFluxCacheFuseOps(struct fuse_operations* ops);

/// Map FluxCache path from FUSE path. root + path, e.g. "/mnt" + "/foo" -> "/mnt/foo".
/// Caller must ensure ctx is valid (from fuse_get_context()->private_data).
std::string FusePathToSdkPath(const FuseContext* ctx, const char* fuse_path);

/// Convert Status to negated errno for FUSE return.
int StatusToErrno(int status_code, int default_errno = -EIO);

}  // namespace fluxcache
