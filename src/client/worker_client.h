#pragma once

#include "common/rpc/channel_pool.h"
#include "common/status.h"
#include <memory>
#include <string>

namespace fluxcache {

namespace proto {
class ReadPagesRequest;
class ReadPagesResponse;
class WritePagesRequest;
class WritePagesResponse;
}

/// Worker RPC wrapper for a single worker address. All RPCs set deadline.
class WorkerClient {
 public:
  /// @param pool Channel pool (must outlive this client).
  /// @param worker_address "host:port".
  /// @param deadline_sec RPC deadline in seconds (default 10).
  WorkerClient(ChannelPool* pool, const std::string& worker_address,
               int deadline_sec = 10);

  /// Read pages from Worker. Returns Unavailable on DEADLINE_EXCEEDED or
  /// UNAVAILABLE.
  Status ReadPages(const proto::ReadPagesRequest& request,
                   proto::ReadPagesResponse* response);

  /// Write pages to Worker. Returns Unavailable on DEADLINE_EXCEEDED or
  /// UNAVAILABLE.
  Status WritePages(const proto::WritePagesRequest& request,
                    proto::WritePagesResponse* response);

 private:
  ChannelPool* pool_;
  std::string worker_address_;
  int deadline_sec_;
};

}  // namespace fluxcache
