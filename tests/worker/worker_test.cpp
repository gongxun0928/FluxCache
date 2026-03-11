#include "worker/worker_service_impl.h"
#include "worker/worker_server.h"
#include "worker/page/page_store.h"
#include "worker/storage/memory_tier.h"
#include "common/config/config.h"
#include "worker.pb.h"
#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fluxcache {

namespace {
constexpr size_t kPageSize = 1024 * 1024;
constexpr size_t kBlockSize = 64ULL * 1024 * 1024;
}  // namespace

TEST(WorkerServiceTest, ReadPagesWithEmptyRequestReturnsError) {
  MemoryTier tier(256 * kPageSize);
  PageStore store(&tier, kPageSize);
  WorkerServiceImpl impl(&store, kPageSize, kBlockSize);
  ::grpc::ServerContext ctx;
  proto::ReadPagesRequest req;
  proto::ReadPagesResponse resp;

  auto status = impl.ReadPages(&ctx, &req, &resp);

  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
}

TEST(WorkerServiceTest, WritePagesReturnsUnimplemented) {
  MemoryTier tier(256 * kPageSize);
  PageStore store(&tier, kPageSize);
  WorkerServiceImpl impl(&store, kPageSize, kBlockSize);
  ::grpc::ServerContext ctx;
  proto::WritePagesRequest req;
  proto::WritePagesResponse resp;

  auto status = impl.WritePages(&ctx, &req, &resp);

  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::UNIMPLEMENTED);
  EXPECT_NE(std::string(status.error_message()).find("WritePages"),
            std::string::npos);
}

TEST(WorkerServiceTest, HeartbeatReturnsOk) {
  MemoryTier tier(256 * kPageSize);
  PageStore store(&tier, kPageSize);
  WorkerServiceImpl impl(&store, kPageSize, kBlockSize);
  ::grpc::ServerContext ctx;
  proto::HeartbeatRequest req;
  req.set_worker_id(1);
  proto::HeartbeatResponse resp;

  auto status = impl.Heartbeat(&ctx, &req, &resp);

  ASSERT_TRUE(status.ok()) << status.error_message();
}

TEST(WorkerServerTest, StartAndShutdown) {
  WorkerConfig cfg;
  cfg.data_dir = "/tmp/fluxcache_worker_test";
  cfg.host = "127.0.0.1";
  cfg.port = 19999;

  WorkerServer server(cfg);
  EXPECT_TRUE(server.Start());
  server.Shutdown();
}

TEST(WorkerConfigTest, LoadConfigWithWorkerFields) {
  auto tmp_dir = std::filesystem::temp_directory_path();
  auto path = tmp_dir / ("fluxcache_worker_config_test_" +
                         std::to_string(std::chrono::steady_clock::now()
                                            .time_since_epoch()
                                            .count()) +
                         ".yaml");
  const char* yaml = R"(
fluxcache:
  master:
    host: "127.0.0.1"
    port: 9090
  worker:
    data_dir: "/tmp/fluxcache_worker"
    host: "0.0.0.0"
    port: 9091
    heartbeat_interval_ms: 3000
  client:
    master_host: "127.0.0.1"
    master_port: 9090
  ufs:
    type: "localfs"
    path: "/tmp/fluxcache_ufs"
)";
  std::ofstream f(path);
  f << yaml;
  f.close();

  auto result = LoadConfig(path.string());
  std::remove(path.string().c_str());

  ASSERT_TRUE(result.ok()) << result.status().message();
  const auto& cfg = result.value();
  EXPECT_EQ(cfg.worker.host, "0.0.0.0");
  EXPECT_EQ(cfg.worker.port, 9091);
  EXPECT_EQ(cfg.worker.heartbeat_interval_ms, 3000u);
}

}  // namespace fluxcache
