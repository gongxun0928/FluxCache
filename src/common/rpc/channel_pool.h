#pragma once

#include <grpcpp/grpcpp.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace fluxcache {

/// Thread-safe pool of gRPC channels, one per address.
/// Phase 1: minimal implementation—each address caches a single Channel.
class ChannelPool {
 public:
  ChannelPool() = default;

  /// Returns a shared_ptr to the Channel for the given address.
  /// Same address returns the same Channel instance; different addresses
  /// return different Channels. Thread-safe.
  /// @param address Target in "host:port" or "dns:port" format.
  std::shared_ptr<grpc::Channel> GetChannel(const std::string& address);

 private:
  std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<grpc::Channel>> channels_;
};

}  // namespace fluxcache
