#pragma once

#include <grpcpp/grpcpp.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fluxcache {

/// Options for ChannelPool: pool size per address.
struct ChannelPoolOptions {
  size_t pool_size_per_address = 4;
};

/// Thread-safe pool of gRPC channels per address.
/// Each address maintains multiple channels; GetChannel returns the next
/// healthy channel in round-robin order.
class ChannelPool {
 public:
  explicit ChannelPool(const ChannelPoolOptions& options = {});

  /// Returns the next channel for the given address (round-robin).
  /// Creates channels on first use; Warmup() can pre-create them.
  /// If all channels are unhealthy, returns the last one (caller will get
  /// RPC failure and may retry).
  std::shared_ptr<grpc::Channel> GetChannel(const std::string& address);

  /// Pre-creates pool_size channels for the address.
  void Warmup(const std::string& address);

  /// Evicts unhealthy channels for the address; next GetChannel will
  /// recreate them.
  void EvictUnhealthy(const std::string& address);

 private:
  bool IsHealthy(const std::shared_ptr<grpc::Channel>& ch) const;
  void EnsurePool(const std::string& address);

  ChannelPoolOptions options_;
  std::mutex mutex_;
  struct PerAddressState {
    std::vector<std::shared_ptr<grpc::Channel>> channels;
    size_t next_index = 0;
  };
  std::unordered_map<std::string, PerAddressState> per_address_;
};

}  // namespace fluxcache
