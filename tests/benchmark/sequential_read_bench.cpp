// P3-03: Sequential read benchmark.
// Compares throughput: no batch/prefetch vs batch vs batch+prefetch.

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

constexpr uint16_t kMasterPort = 29800;
constexpr uint16_t kWorkerPort = 29801;
constexpr size_t kPageSize = 1024 * 1024;  // 1MB
constexpr size_t kFileSize = 16 * 1024 * 1024;  // 16MB
constexpr int kIterations = 10;

struct BenchResult {
  double throughput_mbps = 0;
  double latency_ms = 0;
};

BenchResult RunSequentialRead(ClientConfig cfg, const std::string& path,
                              size_t read_size) {
  FluxCacheClient client(cfg);
  auto start = std::chrono::steady_clock::now();
  size_t total_bytes = 0;
  for (int i = 0; i < kIterations; ++i) {
    auto result = client.Read(path, 0, read_size);
    if (!result.ok()) {
      return {0, 0};
    }
    total_bytes += result.value().size();
  }
  auto end = std::chrono::steady_clock::now();
  double sec = std::chrono::duration<double>(end - start).count();
  double throughput_mbps = (total_bytes / (1024.0 * 1024.0)) / sec;
  double latency_ms = (sec / kIterations) * 1000.0;
  return {throughput_mbps, latency_ms};
}

}  // namespace

int RunSequentialReadBench() {
  namespace fs = std::filesystem;
  auto base = fs::temp_directory_path() / "fluxcache_seq_bench";
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
  RegisterFakeUfsForTest("seq-bench", std::move(fake));

  ClientConfig base_cfg;
  base_cfg.master_host = "127.0.0.1";
  base_cfg.master_port = kMasterPort;
  base_cfg.page_size = kPageSize;
  base_cfg.local_cache_enabled = false;  // Disable L1 to measure RPC path

  FluxCacheClient setup_client(base_cfg);
  if (!setup_client.GetMasterClient()->Mount("/mnt", "fake://seq-bench").ok() ||
      !setup_client.GetMasterClient()->RegisterWorker("127.0.0.1", kWorkerPort).ok()) {
    std::fprintf(stderr, "Failed to setup mount/worker\n");
    return 1;
  }

  const std::string path = "/mnt/bench.dat";
  const size_t read_size = kFileSize;

  // Baseline: no batch, no prefetch
  ClientConfig baseline_cfg = base_cfg;
  baseline_cfg.prefetch_blocks = 0;
  baseline_cfg.batch_read_max_blocks = 1;
  BenchResult baseline = RunSequentialRead(baseline_cfg, path, read_size);

  // Batch only
  ClientConfig batch_cfg = base_cfg;
  batch_cfg.prefetch_blocks = 0;
  batch_cfg.batch_read_max_blocks = 8;
  BenchResult batch = RunSequentialRead(batch_cfg, path, read_size);

  // Batch + prefetch
  ClientConfig full_cfg = base_cfg;
  full_cfg.prefetch_blocks = 2;
  full_cfg.batch_read_max_blocks = 8;
  BenchResult full = RunSequentialRead(full_cfg, path, read_size);

  worker_server->Shutdown();
  master_server->Shutdown();
  try {
    fs::remove_all(base);
  } catch (...) {
  }

  std::printf("Sequential read benchmark (%zu MB file, %d iterations)\n",
              read_size / (1024 * 1024), kIterations);
  std::printf("  Baseline (no batch/prefetch): %.2f MB/s, %.1f ms/read\n",
              baseline.throughput_mbps, baseline.latency_ms);
  std::printf("  Batch only:                 %.2f MB/s, %.1f ms/read\n",
              batch.throughput_mbps, batch.latency_ms);
  std::printf("  Batch + prefetch:           %.2f MB/s, %.1f ms/read\n",
              full.throughput_mbps, full.latency_ms);

  if (batch.throughput_mbps > 0 && baseline.throughput_mbps > 0) {
    double ratio = batch.throughput_mbps / baseline.throughput_mbps;
    std::printf("  Batch vs baseline: %.2fx\n", ratio);
    if (ratio < 1.0) {
      std::fprintf(stderr, "WARNING: batch did not improve throughput\n");
    }
  }

  return 0;
}

}  // namespace fluxcache

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  return fluxcache::RunSequentialReadBench();
}
