#include "common/rpc/channel_pool.h"
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

namespace fluxcache {

std::shared_ptr<grpc::Channel> ChannelPool::GetChannel(const std::string& address) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = channels_.find(address);
  if (it != channels_.end()) {
    return it->second;
  }
  auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
  channels_[address] = channel;
  return channel;
}

}  // namespace fluxcache
