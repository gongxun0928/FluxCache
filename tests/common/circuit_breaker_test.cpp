#include "common/rpc/circuit_breaker.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

namespace fluxcache {

TEST(CircuitBreakerTest, ClosedAllowsRequests) {
  CircuitBreaker::Options opts;
  opts.failure_threshold = 5;
  CircuitBreaker cb(opts);
  EXPECT_EQ(cb.State(), CircuitState::kClosed);
  EXPECT_TRUE(cb.AllowRequest());
}

TEST(CircuitBreakerTest, FailuresExceedThresholdOpens) {
  CircuitBreaker::Options opts;
  opts.failure_threshold = 5;
  CircuitBreaker cb(opts);
  for (int i = 0; i < 5; ++i) {
    cb.RecordFailure();
  }
  EXPECT_EQ(cb.State(), CircuitState::kOpen);
  EXPECT_FALSE(cb.AllowRequest());
}

TEST(CircuitBreakerTest, OpenAfterDurationTransitionsToHalfOpen) {
  CircuitBreaker::Options opts;
  opts.failure_threshold = 2;
  opts.open_duration_ms = 50;
  CircuitBreaker cb(opts);
  cb.RecordFailure();
  cb.RecordFailure();
  EXPECT_EQ(cb.State(), CircuitState::kOpen);
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  EXPECT_TRUE(cb.AllowRequest()) << "Should allow probe in HALF_OPEN";
  EXPECT_EQ(cb.State(), CircuitState::kHalfOpen);
}

TEST(CircuitBreakerTest, HalfOpenProbeSuccessRecoversToClosed) {
  CircuitBreaker::Options opts;
  opts.failure_threshold = 2;
  opts.open_duration_ms = 50;
  opts.half_open_probes = 1;
  CircuitBreaker cb(opts);
  cb.RecordFailure();
  cb.RecordFailure();
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  cb.AllowRequest();
  cb.RecordSuccess();
  EXPECT_EQ(cb.State(), CircuitState::kClosed);
  EXPECT_TRUE(cb.AllowRequest());
}

TEST(CircuitBreakerTest, HalfOpenProbeFailureStaysOpen) {
  CircuitBreaker::Options opts;
  opts.failure_threshold = 2;
  opts.open_duration_ms = 50;
  opts.half_open_probes = 1;
  CircuitBreaker cb(opts);
  cb.RecordFailure();
  cb.RecordFailure();
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  cb.AllowRequest();
  cb.RecordFailure();
  EXPECT_EQ(cb.State(), CircuitState::kOpen);
  EXPECT_FALSE(cb.AllowRequest());
}

TEST(CircuitBreakerTest, SuccessesResetFailureCountInClosed) {
  CircuitBreaker::Options opts;
  opts.failure_threshold = 5;
  CircuitBreaker cb(opts);
  cb.RecordFailure();
  cb.RecordFailure();
  cb.RecordSuccess();
  cb.RecordSuccess();
  cb.RecordFailure();
  cb.RecordFailure();
  cb.RecordFailure();
  EXPECT_EQ(cb.State(), CircuitState::kClosed) << "3 failures after reset, below 5";
  cb.RecordFailure();
  cb.RecordFailure();
  EXPECT_EQ(cb.State(), CircuitState::kOpen);
}

}  // namespace fluxcache
