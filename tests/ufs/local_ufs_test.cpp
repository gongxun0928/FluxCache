#include "ufs/local_ufs.h"
#include "ufs/ufs_factory.h"

#include <filesystem>
#include <gtest/gtest.h>

namespace fluxcache {

namespace {

std::string CreateTempRoot() {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "fluxcache_ufs_test";
  fs::create_directories(tmp);
  return tmp.string() + "/";
}

}  // namespace

class LocalUFSTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = CreateTempRoot();
    ufs_ = std::make_unique<LocalUFS>(root_);
  }

  void TearDown() override {
    ufs_.reset();
    std::filesystem::remove_all(root_);
  }

  std::string root_;
  std::unique_ptr<LocalUFS> ufs_;
};

TEST_F(LocalUFSTest, CreateReadWriteDelete) {
  Status st = ufs_->Write("file.txt", 0, "hello");
  ASSERT_TRUE(st.ok()) << "Write failed";

  std::string out;
  st = ufs_->Read("file.txt", 0, 5, &out);
  ASSERT_TRUE(st.ok()) << "Read failed";
  EXPECT_EQ(out, "hello");

  st = ufs_->Delete("file.txt");
  ASSERT_TRUE(st.ok()) << "Delete failed";

  FileStatus status;
  st = ufs_->GetStatus("file.txt", &status);
  ASSERT_TRUE(st.ok());
  EXPECT_FALSE(status.exists);
}

TEST_F(LocalUFSTest, GetStatusReturnsStableSizeAndMtime) {
  const std::string content = "test content";
  Status st = ufs_->Write("sizetest.txt", 0, content);
  ASSERT_TRUE(st.ok());

  FileStatus status;
  st = ufs_->GetStatus("sizetest.txt", &status);
  ASSERT_TRUE(st.ok());
  EXPECT_TRUE(status.exists);
  EXPECT_FALSE(status.is_directory);
  EXPECT_EQ(status.size, content.size());
  EXPECT_GT(status.mtime_ms, 0);

  st = ufs_->Write("sizetest.txt", 0, "short");
  ASSERT_TRUE(st.ok());
  st = ufs_->GetStatus("sizetest.txt", &status);
  ASSERT_TRUE(st.ok());
  EXPECT_EQ(status.size, 5u);
}

TEST_F(LocalUFSTest, ListReturnsEntriesWithStatus) {
  Status st = ufs_->Mkdirs("dir");
  ASSERT_TRUE(st.ok());
  st = ufs_->Write("dir/file.txt", 0, "data");
  ASSERT_TRUE(st.ok());

  std::vector<FileStatus> entries;
  st = ufs_->List("dir", &entries);
  ASSERT_TRUE(st.ok());
  ASSERT_EQ(entries.size(), 1u);
  EXPECT_EQ(entries[0].path, "file.txt");
  EXPECT_TRUE(entries[0].exists);
  EXPECT_FALSE(entries[0].is_directory);
  EXPECT_EQ(entries[0].size, 4u);
}

TEST_F(LocalUFSTest, GetStatusNonexistentReturnsExistsFalse) {
  FileStatus status;
  Status st = ufs_->GetStatus("nonexistent/path", &status);
  ASSERT_TRUE(st.ok());
  EXPECT_FALSE(status.exists);
}

TEST_F(LocalUFSTest, ReadNonexistentReturnsNotFound) {
  std::string out;
  Status st = ufs_->Read("nonexistent.txt", 0, 10, &out);
  EXPECT_FALSE(st.ok());
  EXPECT_EQ(st.code(), StatusCode::kNotFound);
}

TEST_F(LocalUFSTest, ListNonexistentReturnsNotFound) {
  std::vector<FileStatus> entries;
  Status st = ufs_->List("nonexistent_dir", &entries);
  EXPECT_FALSE(st.ok());
  EXPECT_EQ(st.code(), StatusCode::kNotFound);
}

TEST_F(LocalUFSTest, DeleteNonexistentReturnsNotFound) {
  Status st = ufs_->Delete("nonexistent.txt");
  EXPECT_FALSE(st.ok());
  EXPECT_EQ(st.code(), StatusCode::kNotFound);
}

TEST_F(LocalUFSTest, Rename) {
  Status st = ufs_->Write("old.txt", 0, "content");
  ASSERT_TRUE(st.ok());
  st = ufs_->Rename("old.txt", "new.txt");
  ASSERT_TRUE(st.ok());

  FileStatus status;
  st = ufs_->GetStatus("old.txt", &status);
  ASSERT_TRUE(st.ok());
  EXPECT_FALSE(status.exists);

  st = ufs_->GetStatus("new.txt", &status);
  ASSERT_TRUE(st.ok());
  EXPECT_TRUE(status.exists);
  EXPECT_EQ(status.size, 7u);
}

TEST_F(LocalUFSTest, MkdirsCreatesNestedDirectories) {
  Status st = ufs_->Mkdirs("a/b/c");
  ASSERT_TRUE(st.ok());

  FileStatus status;
  st = ufs_->GetStatus("a", &status);
  ASSERT_TRUE(st.ok());
  EXPECT_TRUE(status.exists);
  EXPECT_TRUE(status.is_directory);

  st = ufs_->GetStatus("a/b", &status);
  ASSERT_TRUE(st.ok());
  EXPECT_TRUE(status.exists);
  EXPECT_TRUE(status.is_directory);

  st = ufs_->GetStatus("a/b/c", &status);
  ASSERT_TRUE(st.ok());
  EXPECT_TRUE(status.exists);
  EXPECT_TRUE(status.is_directory);
}

TEST_F(LocalUFSTest, FactoryCreatesLocalUFS) {
  std::unique_ptr<UFS> ufs;
  Status st = CreateUFS("local", root_, &ufs);
  ASSERT_TRUE(st.ok());
  ASSERT_NE(ufs.get(), nullptr);

  st = ufs->Write("factory_test.txt", 0, "ok");
  ASSERT_TRUE(st.ok());
}

TEST_F(LocalUFSTest, FactoryCreatesS3UfsStub) {
  std::unique_ptr<UFS> ufs;
  Status st = CreateUFS("s3", "bucket", &ufs);
  ASSERT_TRUE(st.ok());
  ASSERT_NE(ufs.get(), nullptr);
  std::string out;
  st = ufs->Read("key", 0, 10, &out);
  EXPECT_FALSE(st.ok());
  EXPECT_EQ(st.code(), StatusCode::kUnavailable);
}

TEST_F(LocalUFSTest, FactoryCreatesHdfsUfsStub) {
  std::unique_ptr<UFS> ufs;
  Status st = CreateUFS("hdfs", "namenode:9000", &ufs);
  ASSERT_TRUE(st.ok());
  ASSERT_NE(ufs.get(), nullptr);
  std::string out;
  st = ufs->Read("/path", 0, 10, &out);
  EXPECT_FALSE(st.ok());
  EXPECT_EQ(st.code(), StatusCode::kUnavailable);
}

TEST_F(LocalUFSTest, FactoryRejectsUnknownScheme) {
  std::unique_ptr<UFS> ufs;
  Status st = CreateUFS("unknown", "authority", &ufs);
  EXPECT_FALSE(st.ok());
  EXPECT_EQ(st.code(), StatusCode::kInvalidArgument);
}

}  // namespace fluxcache
