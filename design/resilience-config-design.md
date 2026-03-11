# 弹性配置层设计（超时 / 重试 / 熔断）

> 日期: 2026-03-11
> 状态: Phase I
> 关联: [P3-05](../issues/P3-05-resilience-config.md)

## 1. 概述

在 P2-08 ChannelPool 与 RetryPolicy 之上，补齐分层超时、受控重试和熔断配置，为后续安全降级（P3-06）提供基础设施。核心边界：**重试、超时、熔断与 P2-08 幂等边界保持一致**。

## 2. 设计决策

### 2.1 分层超时

- **操作类型**：按 RPC 类型区分超时（Master 读 vs Worker 读 vs 写等）。
- **配置结构**：
  - `timeout_master_read_sec`：Master 读操作（GetFileInfo、GetHashRing、ListMounts）：默认 10。
  - `timeout_master_write_sec`：Master 写操作（CreateFile、CompleteFile、DeleteFile、Mount、Unmount）：默认 30。
  - `timeout_worker_read_sec`：Worker ReadPages：默认 10。
  - `timeout_worker_write_sec`：Worker WritePages：默认 30。
- **应用**：MasterClient、WorkerClient 构造时传入对应 deadline；或从 ResilienceConfig 按操作类型取。

### 2.2 重试边界（与 P2-08 一致）

- 幂等读：GetFileInfo、GetHashRing、ReadPages → 可重试。
- 非幂等写：CreateFile、WritePages、CompleteFile、Mount、RegisterWorker → 不自动重试。
- 重试配置沿用 ClientConfig：`retry_max_attempts`、`retry_initial_delay_ms`。
- RetryPolicy 与 ResilienceConfig 解耦：RetryPolicy 仍由 ClientConfig 构建；ResilienceConfig 仅提供超时与熔断。

### 2.3 熔断器

- **状态机**：CLOSED → OPEN → HALF_OPEN → CLOSED。
- **CLOSED**：正常请求；失败时累计错误计数。
- **OPEN**：超过阈值后直接拒绝（返回 Unavailable），不发起 RPC。
- **HALF_OPEN**：经过 `open_duration_ms` 后尝试探测；探测成功 → CLOSED；探测失败 → 保持 OPEN。
- **配置**：
  - `failure_threshold`：错误次数阈值，默认 5。
  - `window_size`：滑动窗口内请求数；或简单计数窗口（最近 N 次）。
  - `error_rate_threshold`：错误率阈值（0.0–1.0），默认 0.5。
  - `open_duration_ms`：OPEN 持续时间，默认 30s。
  - `half_open_probes`：HALF_OPEN 探测成功次数后恢复，默认 1。
- **粒度**：按地址（address）维护熔断器，每个 Master/Worker 地址独立。
- **与幂等**：熔断器在 OPEN 时拒绝所有请求；对幂等读的调用方，上层重试可换地址（Worker 重选）；对 Master 单点，熔断后直接失败。

### 2.4 配置项扩展

- **ClientConfig** 新增：
  - `timeout_master_read_sec`：默认 10。
  - `timeout_master_write_sec`：默认 30。
  - `timeout_worker_read_sec`：默认 10。
  - `timeout_worker_write_sec`：默认 30。
  - `circuit_breaker_enabled`：默认 true。
  - `circuit_breaker_failure_threshold`：默认 5。
  - `circuit_breaker_error_rate_threshold`：默认 0.5。
  - `circuit_breaker_open_duration_ms`：默认 30000。
  - `circuit_breaker_half_open_probes`：默认 1。
- **MasterConfig**：可选扩展，用于 Master 调用 Worker 的弹性；Phase 1 可不扩展，Master 仅作为服务端。

- **ResilienceConfig**：从 ClientConfig 聚合超时与熔断参数，供 RPC 层使用。

## 3. 接口设计

### 3.1 ResilienceConfig

```cpp
// src/common/rpc/resilience_config.h
namespace fluxcache {

enum class OpType {
  kMasterRead,
  kMasterWrite,
  kWorkerRead,
  kWorkerWrite,
};

struct ResilienceConfig {
  int timeout_master_read_sec = 10;
  int timeout_master_write_sec = 30;
  int timeout_worker_read_sec = 10;
  int timeout_worker_write_sec = 30;
  bool circuit_breaker_enabled = true;
  int circuit_breaker_failure_threshold = 5;
  double circuit_breaker_error_rate_threshold = 0.5;
  int circuit_breaker_open_duration_ms = 30000;
  int circuit_breaker_half_open_probes = 1;

  int TimeoutSec(OpType op) const;
};

ResilienceConfig ResilienceConfigFromClientConfig(const ClientConfig& cfg);

}  // namespace fluxcache
```

### 3.2 CircuitBreaker

```cpp
// src/common/rpc/circuit_breaker.h
namespace fluxcache {

enum class CircuitState { kClosed, kOpen, kHalfOpen };

class CircuitBreaker {
 public:
  struct Options {
    int failure_threshold = 5;
    double error_rate_threshold = 0.5;
    int open_duration_ms = 30000;
    int half_open_probes = 1;
  };

  explicit CircuitBreaker(const Options& options = {});

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
  int successes_ = 0;
  int half_open_successes_ = 0;
  std::chrono::steady_clock::time_point open_since_;
  mutable std::mutex mutex_;
  CircuitState state_ = CircuitState::kClosed;
};

}  // namespace fluxcache
```

### 3.3 集成点

- **ChannelPool**：不直接持有熔断器；熔断由调用方（MasterClient/WorkerClient）在发起 RPC 前检查。
- **MasterClient / WorkerClient**：持有 `ResilienceConfig` 和 `CircuitBreaker`（按地址）；在 `ExecuteWithRetry` 前调用 `AllowRequest()`，失败则直接返回 Unavailable；RPC 成功/失败后调用 `RecordSuccess`/`RecordFailure`。
- **FluxCacheClient**：从 ClientConfig 构建 ResilienceConfig，传给 MasterClient；GetWorkerClient 时按地址创建或复用 CircuitBreaker，传给 WorkerClient。

## 4. 幂等性分类（与 P2-08 一致）

| RPC | 幂等 | 重试 | 超时类型 |
|-----|------|------|----------|
| GetHashRing | 是 | 是 | MasterRead |
| GetFileInfo | 是 | 是 | MasterRead |
| ReadPages | 是 | 是 | WorkerRead |
| CreateFile | 否 | 否 | MasterWrite |
| CompleteFile | 否 | 否 | MasterWrite |
| WritePages | 否 | 否 | WorkerWrite |
| DeleteFile | 否 | 否 | MasterWrite |
| Mount/Unmount | 否 | 否 | MasterWrite |

## 5. 测试设计

### 5.1 ResilienceConfig 单元测试

| 场景 | 输入 | 预期 |
|------|------|------|
| 默认超时 | 空 Config | TimeoutSec 返回各类型默认值 |
| 自定义超时 | 设置 timeout_master_read_sec=20 | TimeoutSec(kMasterRead)==20 |
| 从 ClientConfig 构建 | ClientConfig 含新字段 | ResilienceConfig 正确映射 |

### 5.2 CircuitBreaker 单元测试

| 场景 | 实现方式 | 预期 |
|------|----------|------|
| CLOSED 允许请求 | AllowRequest | 返回 true |
| 失败超阈值后 OPEN | 连续 RecordFailure 5 次 | AllowRequest 返回 false |
| OPEN 持续后 HALF_OPEN | 等待 open_duration_ms | AllowRequest 返回 true（探测） |
| HALF_OPEN 探测成功恢复 | RecordSuccess 1 次 | 状态变为 CLOSED |
| HALF_OPEN 探测失败保持 OPEN | RecordFailure | 状态保持 OPEN |
| 错误率超阈值 | 10 次中 6 次失败 | 状态变为 OPEN |

### 5.3 集成验证

- 在 channel_pool_retry_test 或新建 resilience_integration_test 中，验证：熔断 OPEN 时 ExecuteWithRetry 不发起 RPC（直接返回 Unavailable）；HALF_OPEN 探测成功后恢复正常。

## 6. 风险与假设

- **风险**：熔断器在 OPEN 时可能过于激进，影响可用性。**缓解**：阈值可配置；默认值偏保守。
- **假设**：Phase 1 不实现滑动窗口，使用简单计数窗口（最近 N 次请求）。
- **假设**：熔断器按地址维护，FluxCacheClient 需持有 address -> CircuitBreaker 映射；Worker 地址可动态变化，需考虑清理策略（可选：LRU 或固定上限）。

## 7. 非目标

- 指数退避（已由 retry_initial_delay_ms 固定延迟覆盖）。
- 限流、自适应熔断（留待后续）。
