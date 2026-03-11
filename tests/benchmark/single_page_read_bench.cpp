// P3-04: Single-page read benchmark.
// Exercises the Worker ReadPages single-page fast path (zero-copy optimization).
// Each Read(path, offset, page_size) triggers exactly one page fetch.

#include "client/fluxcache_client.h"
#include "common/config/config.h"
#include "master/master_server.h"
#include "ufs/fake_ufs.h"
#include "ufs/ufs_factory.h"
#include "worker/worker_server.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

namespace fluxcache {

namespace {

constexpr uint16_t kMasterPort = 29802;
constexpr uint16_t kWorkerPort = 29803;
constexpr size_t kPageSize = 1024 * 1024;   // 1MB
constexpr size_t kFileSize = 16 * 1024 * 1024;  // 16MB
constexpr int kIterations = 50;

struct BenchResult {
  double throughput_mbps = 0;
  double latency_us = 0;
  int ok_count = 0;
};

BenchResult RunSinglePageReads(ClientConfig cfg, const std::string& path) {
  FluxCacheClient client(cfg);
  auto start = std::chrono::steady_clock::now();
  int ok_count = 0;
  size_t total_bytes = 0;
  for (int i = 0; i < kIterations; ++i) {
    uint64_t offset = static_cast<uint64_t>(i % 16) * kPageSize;
    auto result = client.Read(path, offset, kPageSize);
    if (result.ok()) {
      ok_count++;
      total_bytes += result.value().size();
    }
  }
  auto end = std::chrono::steady_clock::now();
  double sec = std::chrono::duration<double>(end - start).count();
  double throughput_mbps = (total_bytes / (1024.0 * 1024.0)) / sec;
  double latency_us = (sec / kIterations) * 1e6;
  return {throughput_mbps, latency_us, ok_count};
}

}  // namespace

int RunSinglePageReadBench() {
  namespace fs = std::filesystem;
  auto base = fs::temp_directory_path() / "fluxcache_sp_bench";
  auto unique = base / std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  fs::create_directories(unique);
  std::string db_path = (unique / "db").string();
  std::string worker_dir = (unique / "worker").string();

  MasterConfig master_cfg;
  master_cfg.host = "127.0.0.1";
  master_cfg.port = kMasterPort;
  master_cfg.db_path = db_path;

  WorkerConfig worker_cfg;
  worker_cfg.data_dir = worker_dir;
  worker_cfg.host = "127.0.0.1";
  worker_cfg.port = kWorkerPort;

  auto master_server = std::make_unique<MasterServer>(master_cfg);
  auto worker_server = std::make_unique<WorkerServer>(worker_cfg);

  if (!master_server->Start() || !worker_server->Start()) {
    std::fprintf(stderr, "Failed to start servers\n");
    return 1;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  std::string content(kFileSize, 'S');
  for (size_t i = 0; i < kFileSize; i += kPageSize) {
    content[i] = 'A' + (i / kPageSize) % 26;
  }
  auto fake = std::make_unique<FakeUfs>();
  fake->AddFileWithContent("bench.dat", content, 1234567890);
  RegisterFakeUfsForTest("sp-bench", std::move(fake));

  ClientConfig cfg;
  cfg.master_host = "127.0.0.1";
  cfg.master_port = kMasterPort;
  cfg.page_size = kPageSize;
  cfg.local_cache_enabled = false;
  cfg.batch_read_max_blocks = 1;  // Force single-block, single-page RPCs

  FluxCacheClient setup_client(cfg);
  if (!setup_client.GetMasterClient()->Mount("/mnt", "fake://sp-bench").ok() ||
      !setup_client.GetMasterClient()->RegisterWorker("127.0.0.1", kWorkerPort).ok()) {
    std::fprintf(stderr, "Failed to setup mount/worker\n");
    return 1;
  }

  const std::string path = "/mnt/bench.dat";
  BenchResult result = RunSinglePageReads(cfg, path);

  worker_server->Shutdown();
  master_server->Shutdown();
  try {
    fs::remove_all(base);
  } catch (...) {
  }

  std::printf("Single-page read benchmark (P3-04 zero-copy)\n");
  std::printf("  %d iterations, 1 page (1MB) per read\n", kIterations);
  std::printf("  Throughput: %.2f MB/s\n", result.throughput_mbps);
  std::printf("  Latency: %.1f us/read\n", result.latency_us);
  std::printf("  Success: %d/%d\n", result.ok_count, kIterations);

  if (result.ok_count != kIterations) {
    std::fprintf(stderr, "WARNING: not all reads succeeded\n");
    return 1;
  }
  return 0;
}

}  // namespace fluxcache

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  return fluxcache::RunSinglePageReadBench();
}
