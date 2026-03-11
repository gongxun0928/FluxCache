#pragma once

#include "common/rpc/channel_pool.h"
#include "common/rpc/circuit_breaker.h"
#include "common/rpc/resilience_config.h"
#include "common/rpc/retry_policy.h"
#include "common/status.h"
#include <memory>
#include <string>

namespace fluxcache {

namespace proto {
class ReadPagesRequest;
class ReadPagesResponse;
class BatchReadPagesRequest;
class BatchReadPagesResponse;
class WritePagesRequest;
class WritePagesResponse;
}

/// Worker RPC wrapper for a single worker address. All RPCs set deadline.
class WorkerClient {
 public:
  /// @param pool Channel pool (must outlive this client).
  /// @param worker_address "host:port".
  /// @param resilience_config Timeouts per op type.
  /// @param retry_policy Retry policy for idempotent RPCs (ReadPages only).
  /// @param circuit_breaker Optional; if non-null and enabled, blocks when OPEN.
  WorkerClient(ChannelPool* pool, const std::string& worker_address,
               const ResilienceConfig& resilience_config,
               const RetryPolicy& retry_policy = RetryPolicy{},
               CircuitBreaker* circuit_breaker = nullptr);

  /// Read pages from Worker. Returns Unavailable on DEADLINE_EXCEEDED or
  /// UNAVAILABLE.
  Status ReadPages(const proto::ReadPagesRequest& request,
                   proto::ReadPagesResponse* response);

  /// Batch read pages from Worker. Returns Unavailable on DEADLINE_EXCEEDED or
  /// UNAVAILABLE.
  Status BatchReadPages(const proto::BatchReadPagesRequest& request,
                        proto::BatchReadPagesResponse* response);

  /// Write pages to Worker. Returns Unavailable on DEADLINE_EXCEEDED or
  /// UNAVAILABLE.
  Status WritePages(const proto::WritePagesRequest& request,
                    proto::WritePagesResponse* response);

 private:
  ChannelPool* pool_;
  std::string worker_address_;
  ResilienceConfig resilience_config_;
  RetryPolicy retry_policy_;
  CircuitBreaker* circuit_breaker_;
};

}  // namespace fluxcache
