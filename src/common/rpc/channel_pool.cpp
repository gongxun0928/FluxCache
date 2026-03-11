#include "common/rpc/channel_pool.h"
#include <algorithm>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

namespace fluxcache {

ChannelPool::ChannelPool(const ChannelPoolOptions& options)
    : options_(options) {}

bool ChannelPool::IsHealthy(const std::shared_ptr<grpc::Channel>& ch) const {
  if (!ch) return false;
  grpc_connectivity_state s = ch->GetState(true);
  return s == GRPC_CHANNEL_READY || s == GRPC_CHANNEL_IDLE ||
         s == GRPC_CHANNEL_CONNECTING;
}

void ChannelPool::EnsurePool(const std::string& address) {
  auto& state = per_address_[address];
  if (!state.channels.empty()) return;

  size_t n = options_.pool_size_per_address > 0 ? options_.pool_size_per_address
                                                 : 1;
  state.channels.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    state.channels.push_back(
        grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
  }
}

std::shared_ptr<grpc::Channel> ChannelPool::GetChannel(
    const std::string& address) {
  std::lock_guard<std::mutex> lock(mutex_);
  EnsurePool(address);
  auto& state = per_address_[address];
  if (state.channels.empty()) return nullptr;

  size_t start = state.next_index;
  for (size_t i = 0; i < state.channels.size(); ++i) {
    size_t idx = (start + i) % state.channels.size();
    state.next_index = (idx + 1) % state.channels.size();
    auto& ch = state.channels[idx];
    if (IsHealthy(ch)) return ch;
  }
  state.next_index = (start + 1) % state.channels.size();
  return state.channels[start];
}

void ChannelPool::Warmup(const std::string& address) {
  std::lock_guard<std::mutex> lock(mutex_);
  EnsurePool(address);
}

void ChannelPool::EvictUnhealthy(const std::string& address) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = per_address_.find(address);
  if (it == per_address_.end()) return;

  auto& channels = it->second.channels;
  channels.erase(
      std::remove_if(channels.begin(), channels.end(),
                     [this](const std::shared_ptr<grpc::Channel>& ch) {
                       return !IsHealthy(ch);
                     }),
      channels.end());
  it->second.next_index = 0;
}

}  // namespace fluxcache
