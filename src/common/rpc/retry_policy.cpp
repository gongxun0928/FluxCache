#include "common/rpc/retry_policy.h"

namespace fluxcache {

Status ExecuteWithRetry(RetryPolicy policy, std::function<Status()> fn) {
  Status last = fn();
  if (last.ok()) return last;
  if (!policy.is_idempotent || policy.max_retries <= 0) return last;
  if (!IsRetryable(last)) return last;

  for (int attempt = 0; attempt < policy.max_retries; ++attempt) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(policy.initial_delay_ms));
    last = fn();
    if (last.ok()) return last;
    if (!IsRetryable(last)) return last;
  }
  return last;
}

}  // namespace fluxcache
