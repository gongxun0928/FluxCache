#include "common/rpc/channel_pool.h"
#include "master.grpc.pb.h"
#include "worker.grpc.pb.h"
#include <gtest/gtest.h>
#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fluxcache {

TEST(ChannelPoolTest, SameAddressReturnsSameChannelWhenPoolSizeOne) {
  ChannelPool pool(ChannelPoolOptions{1});
  const std::string address = "127.0.0.1:9090";

  auto ch1 = pool.GetChannel(address);
  auto ch2 = pool.GetChannel(address);

  ASSERT_NE(ch1, nullptr);
  ASSERT_NE(ch2, nullptr);
  EXPECT_EQ(ch1.get(), ch2.get());
}

TEST(ChannelPoolTest, PoolSizeConfigurableRoundRobin) {
  ChannelPoolOptions opts;
  opts.pool_size_per_address = 4;
  ChannelPool pool(opts);
  const std::string address = "127.0.0.1:9090";

  std::vector<grpc::Channel*> seen;
  for (int i = 0; i < 8; ++i) {
    auto ch = pool.GetChannel(address);
    ASSERT_NE(ch, nullptr);
    seen.push_back(ch.get());
  }
  EXPECT_EQ(seen[0], seen[4]) << "Round-robin cycles every pool_size";
  EXPECT_EQ(seen[1], seen[5]);
  EXPECT_EQ(seen[2], seen[6]);
  EXPECT_EQ(seen[3], seen[7]);
}

TEST(ChannelPoolTest, WarmupPreCreatesChannels) {
  ChannelPoolOptions opts;
  opts.pool_size_per_address = 3;
  ChannelPool pool(opts);
  const std::string address = "127.0.0.1:9092";

  pool.Warmup(address);

  std::set<grpc::Channel*> unique;
  for (int i = 0; i < 3; ++i) {
    auto ch = pool.GetChannel(address);
    ASSERT_NE(ch, nullptr);
    unique.insert(ch.get());
  }
  EXPECT_EQ(unique.size(), 3u) << "Warmup creates pool_size distinct channels";
}

TEST(ChannelPoolTest, DifferentAddressesReturnDifferentChannels) {
  ChannelPool pool;
  const std::string addr1 = "127.0.0.1:9090";
  const std::string addr2 = "127.0.0.1:9091";

  auto ch1 = pool.GetChannel(addr1);
  auto ch2 = pool.GetChannel(addr2);

  ASSERT_NE(ch1, nullptr);
  ASSERT_NE(ch2, nullptr);
  EXPECT_NE(ch1.get(), ch2.get());
}

TEST(ChannelPoolTest, ThreadSafeConcurrentGetSameAddress) {
  ChannelPool pool(ChannelPoolOptions{1});
  const std::string address = "127.0.0.1:9100";
  constexpr int kNumThreads = 8;
  constexpr int kGetsPerThread = 100;

  std::vector<std::shared_ptr<grpc::Channel>> results(kNumThreads * kGetsPerThread);
  std::vector<std::thread> threads;

  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&pool, &address, &results, t]() {
      for (int i = 0; i < kGetsPerThread; ++i) {
        results[t * kGetsPerThread + i] = pool.GetChannel(address);
      }
    });
  }

  for (auto& th : threads) {
    th.join();
  }

  grpc::Channel* first = results[0].get();
  ASSERT_NE(first, nullptr);
  for (size_t i = 1; i < results.size(); ++i) {
    EXPECT_EQ(results[i].get(), first) << "All concurrent GetChannel for same address must return same Channel";
  }
}

TEST(ChannelPoolTest, ThreadSafeConcurrentGetDifferentAddresses) {
  ChannelPool pool;
  constexpr int kNumAddresses = 4;
  constexpr int kNumThreads = 8;
  constexpr int kGetsPerThread = 50;

  std::vector<std::string> addresses;
  for (int i = 0; i < kNumAddresses; ++i) {
    addresses.push_back("127.0.0.1:" + std::to_string(9200 + i));
  }

  std::atomic<bool> done{false};
  std::vector<std::thread> threads;

  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&pool, &addresses, &done]() {
      while (!done.load()) {
        for (const auto& addr : addresses) {
          auto ch = pool.GetChannel(addr);
          (void)ch;
        }
      }
    });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  done.store(true);

  for (auto& th : threads) {
    th.join();
  }

  for (const auto& addr : addresses) {
    auto ch = pool.GetChannel(addr);
    EXPECT_NE(ch, nullptr) << "Channel for " << addr << " should be valid";
  }
}

TEST(ChannelPoolTest, ChannelUsableForMasterStub) {
  ChannelPool pool;
  auto channel = pool.GetChannel("127.0.0.1:9090");
  ASSERT_NE(channel, nullptr);

  auto stub = fluxcache::proto::MasterService::NewStub(channel);
  EXPECT_NE(stub, nullptr);
}

TEST(ChannelPoolTest, ChannelUsableForWorkerStub) {
  ChannelPool pool;
  auto channel = pool.GetChannel("127.0.0.1:9091");
  ASSERT_NE(channel, nullptr);

  auto stub = fluxcache::proto::WorkerService::NewStub(channel);
  EXPECT_NE(stub, nullptr);
}

TEST(ChannelPoolTest, EvictUnhealthyDoesNotCrash) {
  ChannelPool pool(ChannelPoolOptions{2});
  const std::string address = "127.0.0.1:9093";

  pool.Warmup(address);
  pool.EvictUnhealthy(address);

  auto ch = pool.GetChannel(address);
  EXPECT_NE(ch, nullptr) << "GetChannel after EvictUnhealthy should work";
}

}  // namespace fluxcache
