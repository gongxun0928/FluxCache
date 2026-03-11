#include "common/rpc/circuit_breaker.h"
#include "common/metrics/metrics_registry.h"

namespace fluxcache {

CircuitBreaker::CircuitBreaker(const Options& options)
    : options_(options),
      open_since_(std::chrono::steady_clock::now()) {}

bool CircuitBreaker::AllowRequest() {
  std::lock_guard<std::mutex> lock(mutex_);
  MaybeTransition();
  if (state_ == CircuitState::kClosed) {
    return true;
  }
  if (state_ == CircuitState::kHalfOpen) {
    return true;
  }
  return false;
}

void CircuitBreaker::RecordSuccess() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == CircuitState::kClosed) {
    failures_ = 0;
    return;
  }
  if (state_ == CircuitState::kHalfOpen) {
    half_open_successes_++;
    if (half_open_successes_ >= options_.half_open_probes) {
      state_ = CircuitState::kClosed;
      failures_ = 0;
      half_open_successes_ = 0;
    }
    return;
  }
}

void CircuitBreaker::RecordFailure() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == CircuitState::kClosed) {
    failures_++;
    if (failures_ >= options_.failure_threshold) {
      state_ = CircuitState::kOpen;
      open_since_ = std::chrono::steady_clock::now();
      if (options_.metrics) {
        options_.metrics->IncCounter("fluxcache_client_breaker_opens_total");
      }
    }
    return;
  }
  if (state_ == CircuitState::kHalfOpen) {
    state_ = CircuitState::kOpen;
    half_open_successes_ = 0;
    open_since_ = std::chrono::steady_clock::now();
    if (options_.metrics) {
      options_.metrics->IncCounter("fluxcache_client_breaker_opens_total");
    }
    return;
  }
}

CircuitState CircuitBreaker::State() const {
  std::lock_guard<std::mutex> lock(mutex_);
  const_cast<CircuitBreaker*>(this)->MaybeTransition();
  return state_;
}

void CircuitBreaker::MaybeTransition() {
  if (state_ != CircuitState::kOpen) return;
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - open_since_);
  if (elapsed.count() >= options_.open_duration_ms) {
    state_ = CircuitState::kHalfOpen;
    half_open_successes_ = 0;
  }
}

}  // namespace fluxcache
