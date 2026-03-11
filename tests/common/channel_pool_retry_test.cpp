#include "common/rpc/retry_policy.h"
#include <gtest/gtest.h>
#include <atomic>

namespace fluxcache {

TEST(ChannelPoolRetryTest, IdempotentReadRetriesOnUnavailable) {
  std::atomic<int> call_count{0};
  RetryPolicy policy;
  policy.max_retries = 3;
  policy.initial_delay_ms = 1;  // Short delay for test
  policy.is_idempotent = true;

  auto fn = [&call_count]() -> Status {
    int n = call_count.fetch_add(1);
    if (n < 2) {
      return Status::Unavailable("injected");
    }
    return Status::OK();
  };

  Status s = ExecuteWithRetry(policy, fn);
  EXPECT_TRUE(s.ok()) << "Should succeed after retries";
  EXPECT_EQ(call_count.load(), 3) << "Initial + 2 retries = 3 calls";
}

TEST(ChannelPoolRetryTest, NonIdempotentWriteNoRetry) {
  std::atomic<int> call_count{0};
  RetryPolicy policy;
  policy.max_retries = 3;
  policy.initial_delay_ms = 1;
  policy.is_idempotent = false;

  auto fn = [&call_count]() -> Status {
    call_count.fetch_add(1);
    return Status::Unavailable("injected");
  };

  Status s = ExecuteWithRetry(policy, fn);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kUnavailable);
  EXPECT_EQ(call_count.load(), 1) << "No retry for non-idempotent";
}

TEST(ChannelPoolRetryTest, IdempotentRetryExhaustedReturnsLastError) {
  std::atomic<int> call_count{0};
  RetryPolicy policy;
  policy.max_retries = 2;
  policy.initial_delay_ms = 1;
  policy.is_idempotent = true;

  auto fn = [&call_count]() -> Status {
    call_count.fetch_add(1);
    return Status::Unavailable("always fail");
  };

  Status s = ExecuteWithRetry(policy, fn);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kUnavailable);
  EXPECT_EQ(call_count.load(), 3) << "Initial + 2 retries = 3 calls";
}

TEST(ChannelPoolRetryTest, StatusOrIdempotentRetriesOnUnavailable) {
  std::atomic<int> call_count{0};
  RetryPolicy policy;
  policy.max_retries = 3;
  policy.initial_delay_ms = 1;
  policy.is_idempotent = true;

  auto fn = [&call_count]() -> StatusOr<int> {
    int n = call_count.fetch_add(1);
    if (n < 2) {
      return Status::Unavailable("injected");
    }
    return 42;
  };

  auto result = ExecuteWithRetry<int>(policy, fn);
  EXPECT_TRUE(result.ok()) << "Should succeed after retries";
  EXPECT_EQ(result.value(), 42);
  EXPECT_EQ(call_count.load(), 3);
}

TEST(ChannelPoolRetryTest, StatusOrNonIdempotentNoRetry) {
  std::atomic<int> call_count{0};
  RetryPolicy policy;
  policy.max_retries = 3;
  policy.is_idempotent = false;

  auto fn = [&call_count]() -> StatusOr<int> {
    call_count.fetch_add(1);
    return Status::Unavailable("injected");
  };

  auto result = ExecuteWithRetry<int>(policy, fn);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(call_count.load(), 1);
}

TEST(ChannelPoolRetryTest, NonRetryableErrorNotRetried) {
  std::atomic<int> call_count{0};
  RetryPolicy policy;
  policy.max_retries = 3;
  policy.is_idempotent = true;

  auto fn = [&call_count]() -> Status {
    call_count.fetch_add(1);
    return Status::NotFound("not retryable");
  };

  Status s = ExecuteWithRetry(policy, fn);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kNotFound);
  EXPECT_EQ(call_count.load(), 1) << "NotFound is not retried";
}

}  // namespace fluxcache
