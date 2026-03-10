#include "common/config/config.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fluxcache {

namespace {

std::string WriteTempYaml(const std::string& content) {
  auto tmp_dir = std::filesystem::temp_directory_path();
  auto path = tmp_dir / ("fluxcache_config_test_" +
                         std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) +
                         ".yaml");
  std::ofstream f(path);
  f << content;
  f.close();
  return path.string();
}

}  // namespace

TEST(ConfigTest, LoadValidConfig) {
  const char* yaml = R"(
fluxcache:
  master:
    host: "127.0.0.1"
    port: 9090
  worker:
    data_dir: "/tmp/fluxcache_worker"
  client:
    master_host: "127.0.0.1"
    master_port: 9090
  ufs:
    type: "localfs"
    path: "/tmp/fluxcache_ufs"
)";
  std::string path = WriteTempYaml(yaml);
  auto result = LoadConfig(path);
  std::remove(path.c_str());

  ASSERT_TRUE(result.ok()) << "LoadConfig failed: " << result.status().message();
  const auto& cfg = result.value();
  EXPECT_EQ(cfg.master.host, "127.0.0.1");
  EXPECT_EQ(cfg.master.port, 9090);
  EXPECT_EQ(cfg.worker.data_dir, "/tmp/fluxcache_worker");
  EXPECT_EQ(cfg.client.master_host, "127.0.0.1");
  EXPECT_EQ(cfg.client.master_port, 9090);
  EXPECT_EQ(cfg.ufs.type, "localfs");
  EXPECT_EQ(cfg.ufs.path, "/tmp/fluxcache_ufs");
}

TEST(ConfigTest, MissingRequiredField) {
  const char* yaml = R"(
fluxcache:
  master:
    port: 9090
  worker:
    data_dir: "/tmp/fluxcache_worker"
  client:
    master_host: "127.0.0.1"
    master_port: 9090
  ufs:
    type: "localfs"
    path: "/tmp/fluxcache_ufs"
)";
  std::string path = WriteTempYaml(yaml);
  auto result = LoadConfig(path);
  std::remove(path.c_str());

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);
  EXPECT_FALSE(result.status().message().empty());
  EXPECT_NE(result.status().message().find("master.host"), std::string::npos);
}

TEST(ConfigTest, TypeErrorPortNotInteger) {
  const char* yaml = R"(
fluxcache:
  master:
    host: "127.0.0.1"
    port: "abc"
  worker:
    data_dir: "/tmp/fluxcache_worker"
  client:
    master_host: "127.0.0.1"
    master_port: 9090
  ufs:
    type: "localfs"
    path: "/tmp/fluxcache_ufs"
)";
  std::string path = WriteTempYaml(yaml);
  auto result = LoadConfig(path);
  std::remove(path.c_str());

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);
  EXPECT_FALSE(result.status().message().empty());
}

TEST(ConfigTest, FileNotFound) {
  auto result = LoadConfig("/nonexistent/fluxcache_config_xyz.yaml");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kIOError);
}

TEST(ConfigTest, InvalidYaml) {
  const char* yaml = "invalid: yaml: content: [";
  std::string path = WriteTempYaml(yaml);
  auto result = LoadConfig(path);
  std::remove(path.c_str());

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);
}

TEST(ConfigTest, EmptyYaml) {
  std::string path = WriteTempYaml("");
  auto result = LoadConfig(path);
  std::remove(path.c_str());

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);
}

TEST(ConfigTest, UfsTypeNotLocalfs) {
  const char* yaml = R"(
fluxcache:
  master:
    host: "127.0.0.1"
    port: 9090
  worker:
    data_dir: "/tmp/fluxcache_worker"
  client:
    master_host: "127.0.0.1"
    master_port: 9090
  ufs:
    type: "s3"
    path: "/bucket"
)";
  std::string path = WriteTempYaml(yaml);
  auto result = LoadConfig(path);
  std::remove(path.c_str());

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kInvalidArgument);
  EXPECT_NE(result.status().message().find("localfs"), std::string::npos);
}

}  // namespace fluxcache
