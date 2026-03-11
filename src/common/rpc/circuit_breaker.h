#pragma once

#include <chrono>
#include <mutex>

namespace fluxcache {

class MetricsRegistry;

enum class CircuitState { kClosed, kOpen, kHalfOpen };

class CircuitBreaker {
 public:
  struct Options {
    int failure_threshold = 5;
    double error_rate_threshold = 0.5;
    int open_duration_ms = 30000;
    int half_open_probes = 1;
    MetricsRegistry* metrics = nullptr;
  };

  explicit CircuitBreaker(const Options& options);

  /// Returns true if request is allowed (CLOSED or HALF_OPEN probe).
  bool AllowRequest();

  /// Record success. In HALF_OPEN, may transition to CLOSED.
  void RecordSuccess();

  /// Record failure. In CLOSED, may transition to OPEN.
  void RecordFailure();

  CircuitState State() const;

 private:
  void MaybeTransition();
  Options options_;
  int failures_ = 0;
  int half_open_successes_ = 0;
  std::chrono::steady_clock::time_point open_since_;
  mutable std::mutex mutex_;
  CircuitState state_ = CircuitState::kClosed;
};

}  // namespace fluxcache
