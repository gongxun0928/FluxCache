// P3-02: PageStore read/write benchmark.
// Measures concurrent read throughput and mixed read-write latency.

#include "worker/page/page_store.h"
#include "worker/storage/memory_tier.h"
#include "common/types.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace fluxcache {

namespace {

constexpr size_t kPageSize = 1024;
constexpr size_t kTierCapacity = 4 * 1024 * 1024;  // 4MB
constexpr int kNumPages = 512;
constexpr int kReadThreads = 8;
constexpr int kMixedReadRatio = 8;   // 8:2 read:write
constexpr int kMixedWriteRatio = 2;
constexpr int kReadOpsPerThread = 10000;
constexpr int kMixedOpsPerThread = 2000;

void RunReadThroughputBench(PageStore* store, int64_t mtime) {
  std::vector<std::thread> threads;
  std::atomic<uint64_t> total_ops{0};
  auto start = std::chrono::steady_clock::now();

  for (int t = 0; t < kReadThreads; ++t) {
    threads.emplace_back([&, t]() {
      std::mt19937 rng(t + 42);
      std::uniform_int_distribution<int> dist(0, kNumPages - 1);
      std::string out;
      for (int i = 0; i < kReadOpsPerThread; ++i) {
        int idx = dist(rng);
        PageId id{MakeBlockId(1 + idx / 64, 0), static_cast<uint16_t>(idx % 64)};
        if (store->GetPage(id, mtime, &out).ok()) {
          total_ops++;
        }
      }
    });
  }
  for (auto& th : threads) th.join();

  auto end = std::chrono::steady_clock::now();
  double sec = std::chrono::duration<double>(end - start).count();
  double ops_per_sec = total_ops.load() / sec;

  std::printf("Read throughput: %.0f ops/s (%d threads, %d ops/thread)\n",
              ops_per_sec, kReadThreads, kReadOpsPerThread);
}

void RunMixedReadWriteBench(PageStore* store, int64_t mtime) {
  std::vector<std::thread> threads;
  std::vector<double> latencies;
  std::mutex lat_mu;

  for (int t = 0; t < kReadThreads; ++t) {
    threads.emplace_back([&, t]() {
      std::mt19937 rng(t + 100);
      std::uniform_int_distribution<int> page_dist(0, kNumPages - 1);
      std::uniform_int_distribution<int> op_dist(0, 9);
      std::string out;
      std::string data(kPageSize, 'x');

      for (int i = 0; i < kMixedOpsPerThread; ++i) {
        int idx = page_dist(rng);
        PageId id{MakeBlockId(1 + idx / 64, 0), static_cast<uint16_t>(idx % 64)};

        auto t0 = std::chrono::steady_clock::now();
        if (op_dist(rng) < kMixedReadRatio) {
          store->GetPage(id, mtime, &out);
        } else {
          store->PutPage(id, data, mtime);
        }
        auto t1 = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        {
          std::lock_guard<std::mutex> lk(lat_mu);
          latencies.push_back(us);
        }
      }
    });
  }
  for (auto& th : threads) th.join();

  std::sort(latencies.begin(), latencies.end());
  size_t n = latencies.size();
  double p50 = n > 0 ? latencies[n / 2] : 0;
  double p99 = n > 0 ? latencies[static_cast<size_t>(n * 0.99)] : 0;

  std::printf("Mixed read-write (8:2) latency: P50=%.1f us, P99=%.1f us\n",
              p50, p99);
}

}  // namespace

int RunPageStoreBench() {
  MemoryTier tier(kTierCapacity);
  PageStore store(&tier, kPageSize);

  const int64_t mtime = 12345;
  std::string data(kPageSize, 'a');

  for (int i = 0; i < kNumPages; ++i) {
    PageId id{MakeBlockId(1 + i / 64, 0), static_cast<uint16_t>(i % 64)};
    if (!store.PutPage(id, data, mtime).ok()) break;
  }

  std::printf("PageStore benchmark (page_size=%zu, %d pages preloaded)\n",
              kPageSize, kNumPages);
  RunReadThroughputBench(&store, mtime);
  RunMixedReadWriteBench(&store, mtime);

  return 0;
}

}  // namespace fluxcache

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  return fluxcache::RunPageStoreBench();
}
