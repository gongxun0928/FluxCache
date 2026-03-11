#include "common/rpc/retry_policy.h"
#include "common/metrics/metrics_registry.h"

namespace fluxcache {

Status ExecuteWithRetry(RetryPolicy policy, std::function<Status()> fn) {
  return ExecuteWithRetry(policy, fn, nullptr, "");
}

Status ExecuteWithRetry(RetryPolicy policy, std::function<Status()> fn,
                       MetricsRegistry* metrics,
                       const std::string& service) {
  Status last = fn();
  if (last.ok()) return last;
  if (!policy.is_idempotent || policy.max_retries <= 0) return last;
  if (!IsRetryable(last)) return last;

  for (int attempt = 0; attempt < policy.max_retries; ++attempt) {
    if (metrics && !service.empty()) {
      metrics->IncCounter("fluxcache_client_retries_total", "service", service);
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(policy.initial_delay_ms));
    last = fn();
    if (last.ok()) return last;
    if (!IsRetryable(last)) return last;
  }
  return last;
}

}  // namespace fluxcache
