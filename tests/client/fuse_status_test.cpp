#include <gtest/gtest.h>

#include <cerrno>

#include "client/fuse/fuse_ops.h"
#include "common/status.h"

namespace fluxcache {

TEST(FuseStatusTest, DirectoryNotEmptyMapsToEnotempty) {
  EXPECT_EQ(StatusToErrno(static_cast<int>(StatusCode::kDirectoryNotEmpty)),
            -ENOTEMPTY);
}

}  // namespace fluxcache
