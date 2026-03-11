#pragma once

#include "common/config/config.h"
#include "common/metrics/metrics_registry.h"
#include "common/status.h"
#include <chrono>
#include <functional>
#include <string>
#include <thread>

namespace fluxcache {

/// Policy for retrying RPCs. Only idempotent operations should retry.
struct RetryPolicy {
  int max_retries = 3;
  int initial_delay_ms = 50;
  bool is_idempotent = true;
};

/// Build RetryPolicy from ClientConfig. Idempotent by default; caller
/// overrides is_idempotent per operation.
inline RetryPolicy RetryPolicyFromConfig(const ClientConfig& cfg) {
  RetryPolicy p;
  p.max_retries = cfg.retry_max_attempts > 0 ? cfg.retry_max_attempts : 0;
  p.initial_delay_ms =
      cfg.retry_initial_delay_ms > 0 ? cfg.retry_initial_delay_ms : 50;
  p.is_idempotent = true;
  return p;
}

/// Returns true if status is retryable (transient failure).
inline bool IsRetryable(const Status& s) {
  return s.code() == StatusCode::kUnavailable;
}

/// Execute an RPC with retry. For idempotent operations, retries on
/// Unavailable up to max_retries. For non-idempotent, no retry.
Status ExecuteWithRetry(RetryPolicy policy, std::function<Status()> fn);

/// Same as above, with optional metrics for retry count.
Status ExecuteWithRetry(RetryPolicy policy, std::function<Status()> fn,
                       MetricsRegistry* metrics,
                       const std::string& service);

/// Execute an RPC returning StatusOr with retry.
template <typename T>
StatusOr<T> ExecuteWithRetry(RetryPolicy policy,
                            std::function<StatusOr<T>()> fn) {
  return ExecuteWithRetry<T>(policy, fn, nullptr, "");
}

/// Same as above, with optional metrics for retry count.
template <typename T>
StatusOr<T> ExecuteWithRetry(RetryPolicy policy,
                            std::function<StatusOr<T>()> fn,
                            MetricsRegistry* metrics,
                            const std::string& service) {
  StatusOr<T> last = fn();
  if (last.ok()) return last;
  if (!policy.is_idempotent || policy.max_retries <= 0) return last;
  if (!IsRetryable(last.status())) return last;

  for (int attempt = 0; attempt < policy.max_retries; ++attempt) {
    if (metrics && !service.empty()) {
      metrics->IncCounter("fluxcache_client_retries_total", "service", service);
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(policy.initial_delay_ms));
    last = fn();
    if (last.ok()) return last;
    if (!IsRetryable(last.status())) return last;
  }
  return last;
}

}  // namespace fluxcache
