#include "ufs/ufs_factory.h"
#include "ufs/local_ufs.h"

namespace fluxcache {

Status CreateUFS(const std::string& scheme, const std::string& authority,
                 std::unique_ptr<UFS>* out) {
  if (!out) return Status::InvalidArgument(nullptr);

  if (scheme == "local" || scheme == "file") {
    *out = std::make_unique<LocalUFS>(authority);
    return Status::OK();
  }
  return Status::InvalidArgument(nullptr);
}

}  // namespace fluxcache
