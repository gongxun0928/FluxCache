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

TEST(StatusTest, DirectoryNotEmptyIsNotOk) {
  Status s = Status::DirectoryNotEmpty("directory not empty");
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kDirectoryNotEmpty);
  EXPECT_EQ(s.message(), "directory not empty");
}

}  // namespace fluxcache
