# ChannelPool 完整实现设计

> 日期: 2026-03-11
> 状态: Phase E
> 关联: [P2-08](../issues/P2-08-channel-pool-full.md)

## 1. 概述

在 P1-04 最小 ChannelPool（每地址单 Channel）之上，补齐生产前必需的 transport 能力：每地址多 Channel 池、轮询负载分发、连接预热、基础健康检查、幂等重试策略。核心边界：**只对幂等操作自动重试**。

## 2. 设计决策

### 2.1 每地址多 Channel 池

- 每个 address 维护一个 Channel 向量，大小可配置（默认 4）。
- `GetChannel(address)` 改为轮询返回下一个 Channel，实现等价负载分发。
- 线程安全：使用 mutex 保护 per-address 状态与轮询索引。

### 2.2 轮询负载分发

- 使用原子或受锁保护的 `next_index` 实现 round-robin。
- 等价于轮询：每次 GetChannel 返回 `channels_[index % size]`，index 递增。

### 2.3 连接预热

- 在首次 `GetChannel(address)` 或显式 `Warmup(address)` 时，一次性创建 `pool_size` 个 Channel。
- 不阻塞：创建后立即返回，gRPC 连接为异步建立。

### 2.4 基础健康检查

- 使用 `grpc::Channel::GetState(true)` 探测连接状态。
- 状态映射：`READY` 为健康；`TRANSIENT_FAILURE`、`SHUTDOWN` 为不健康。
- 不健康 Channel：从池中剔除，下次 GetChannel 时重建；或后台异步重建后重新加入。
- Phase 1 策略：GetChannel 时若选中 Channel 不健康，跳过并尝试下一个；若全部不健康，返回最后一个（调用方将收到 RPC 失败，可触发重试或上层重选）。

### 2.5 RetryPolicy

- **位置**：`common/rpc/retry_policy.h`，与 ChannelPool 解耦。
- **结构**：
  - `max_retries`: 最大重试次数（默认 3）。
  - `retryable_statuses`: UNAVAILABLE、DEADLINE_EXCEEDED。
  - `is_idempotent`: 操作是否幂等，由调用方传入。
- **规则**：
  - 幂等读（GetFileInfo、GetHashRing、ReadPages）：可自动重试。
  - 非幂等写（CreateFile、WritePages、CompleteFile、Mount、RegisterWorker）：默认不自动重试（max_retries=0 或 is_idempotent=false）。
- **应用层**：MasterClient、WorkerClient 在发起 RPC 时，根据操作类型传入 is_idempotent，并执行重试循环（含简单退避，如固定 50ms 或指数退避）。

### 2.6 配置扩展

- `ClientConfig` 新增（可选）：
  - `channel_pool_size`: 每地址 Channel 数，默认 4。
  - `retry_max_attempts`: 幂等操作最大重试次数，默认 3。
  - `retry_initial_delay_ms`: 首次重试延迟，默认 50。

## 3. 接口设计

### 3.1 ChannelPool

```cpp
// channel_pool.h
struct ChannelPoolOptions {
  size_t pool_size_per_address = 4;  // 每地址 Channel 数
};

class ChannelPool {
 public:
  explicit ChannelPool(const ChannelPoolOptions& options = {});

  /// 轮询返回下一个健康 Channel；若均不健康则返回最后一个。
  std::shared_ptr<grpc::Channel> GetChannel(const std::string& address);

  /// 预热：为 address 预创建 pool_size 个 Channel。
  void Warmup(const std::string& address);

  /// 剔除 address 下不健康 Channel，下次 GetChannel 时重建。
  void EvictUnhealthy(const std::string& address);

 private:
  ChannelPoolOptions options_;
  std::mutex mutex_;
  struct PerAddressState {
    std::vector<std::shared_ptr<grpc::Channel>> channels;
    size_t next_index = 0;
  };
  std::unordered_map<std::string, PerAddressState> per_address_;
};
```

### 3.2 RetryPolicy

```cpp
// retry_policy.h
struct RetryPolicy {
  int max_retries = 3;
  int initial_delay_ms = 50;
  bool is_idempotent = true;  // 由调用方按操作设置
};

/// 执行可重试 RPC：若失败且可重试，则延迟后重试。
/// 返回最后一次调用的 Status。
template <typename Fn>
Status ExecuteWithRetry(RetryPolicy policy, Fn&& fn);
```

### 3.3 集成点

- `MasterClient`、`WorkerClient`：构造函数增加 `RetryPolicy` 或从 `ClientConfig` 解析。
- `FluxCacheClient`：构造时传入 `ChannelPoolOptions` 与 `RetryPolicy`。

## 4. 幂等性分类

| RPC | 幂等 | 默认重试 |
|-----|------|----------|
| GetHashRing | 是 | 是 |
| GetFileInfo | 是 | 是 |
| ReadPages | 是 | 是 |
| CreateFile | 否 | 否 |
| CompleteFile | 否* | 否 |
| WritePages | 否 | 否 |
| Mount | 否 | 否 |
| RegisterWorker | 否 | 否 |

\* CompleteFile 同参数多次调用语义上可幂等，但为安全起见与写路径一致，不自动重试。

## 5. 测试设计

### 5.1 单元测试（channel_pool_test.cpp）

| 场景 | 输入 | 预期 |
|------|------|------|
| 池大小可配置 | pool_size=4，GetChannel 多次 | 轮询返回 4 个不同 Channel |
| 预热 | Warmup 后 GetChannel | 池已满，无懒创建竞争 |
| 健康检查 | 注入不健康 Channel | EvictUnhealthy 后 GetChannel 返回新 Channel |

### 5.2 重试边界测试（channel_pool_retry_test.cpp）

| 场景 | 实现方式 | 预期 |
|------|----------|------|
| 幂等读可重试 | 伪 transport：前 N 次返回 UNAVAILABLE，第 N+1 次成功 | 最终成功，重试次数 ≤ max_retries |
| 非幂等写不重试 | 伪 transport：首次返回 UNAVAILABLE | 直接返回失败，无重试 |
| 重试耗尽仍失败 | 伪 transport：始终返回 UNAVAILABLE | 返回 UNAVAILABLE，重试次数 = max_retries |

**伪 transport 实现**：使用 in-process gRPC 服务，内部维护失败计数器；前 `fail_count` 次返回 `grpc::Status(grpc::StatusCode::UNAVAILABLE, "injected")`，之后返回 OK。测试通过 `fail_count` 控制注入行为。

## 6. 风险与假设

- **风险**：gRPC `GetState(true)` 可能触发连接尝试，影响延迟。**缓解**：仅在 EvictUnhealthy 或 GetChannel 选 Channel 时调用，频率可控。
- **假设**：Phase 1 不实现指数退避，使用固定延迟即可。
- **假设**：FluxCacheClient 持有 ChannelPool，ClientConfig 扩展后需在 main/初始化路径传入新字段；若 YAML 无新字段则使用默认值。

## 7. 非目标

- 指数退避、熔断、限流（留待 P3-05 弹性配置层）。
- 跨地址负载均衡（由 CachedHashRing 负责 Worker 选择）。
