#include "common/status.h"
#include <gtest/gtest.h>

namespace fluxcache {

TEST(StatusTest, OkStatus) {
  Status s = Status::OK();
  EXPECT_TRUE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kOk);
}

TEST(StatusTest, NotFoundIsNotOk) {
  Status s = Status::NotFound();
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kNotFound);
}

}  // namespace fluxcache
