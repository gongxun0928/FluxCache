#pragma once

#include "common/config/config.h"
#include "common/rpc/channel_pool.h"
#include "common/status.h"
#include "master.pb.h"
#include <memory>
#include <string>

namespace fluxcache {

/// Master RPC wrapper. All RPCs set deadline.
class MasterClient {
 public:
  /// @param pool Channel pool (must outlive this client).
  /// @param master_address "host:port".
  /// @param deadline_sec RPC deadline in seconds (default 10).
  MasterClient(ChannelPool* pool, const std::string& master_address,
               int deadline_sec = 10);

  /// Get hash ring from Master. Returns Unavailable on DEADLINE_EXCEEDED or
  /// UNAVAILABLE.
  StatusOr<proto::GetHashRingResponse> GetHashRing();

 private:
  ChannelPool* pool_;
  std::string master_address_;
  int deadline_sec_;
};

}  // namespace fluxcache
