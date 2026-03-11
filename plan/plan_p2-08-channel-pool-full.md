# Plan: P2-08 ChannelPool 完整实现与幂等重试边界

> Status: Done

## Goal

- **Problem**: 当前 ChannelPool 每地址仅单 Channel，无重试策略，网络抖动时读路径易失败。
- **Target outcome**: 每地址多 Channel 池、轮询负载分发、连接预热、基础健康检查、retry_policy；幂等读可自动重试，非幂等写不重试；测试通过伪 transport 验证重试边界。

## Steps

1. 扩展 `ClientConfig` 与 `ChannelPoolOptions`：`channel_pool_size`、`retry_max_attempts`、`retry_initial_delay_ms`。
2. 实现 `ChannelPool` 多 Channel 池：`GetChannel` 轮询、`Warmup`、`EvictUnhealthy`、健康检查。
3. 实现 `RetryPolicy` 与 `ExecuteWithRetry`：`common/rpc/retry_policy.h/.cpp`。
4. 集成到 `MasterClient`、`WorkerClient`：幂等 RPC 使用 `ExecuteWithRetry`，非幂等直接调用。
5. 更新 `FluxCacheClient`：传入 `ChannelPoolOptions` 与 `RetryPolicy`。
6. 扩展 `channel_pool_test.cpp`：池大小、轮询、预热、健康检查。
7. 实现 `channel_pool_retry_test.cpp`：in-process 伪服务注入 UNAVAILABLE，验证幂等读重试、非幂等写不重试、重试耗尽。

## Risks & Assumptions

- **Risk**: gRPC GetState 可能带来额外延迟。**Mitigation**: 仅在选 Channel 时调用，且可先尝试 READY 再 fallback。
- **Assumption**: 配置加载支持新字段；无则用默认值。

## To Confirm

- [x] 设计文档 `design/channel-pool-design.md` 已产出。
- [x] 验收标准以 `issues/P2-08-channel-pool-full.md` 为准。

## 变更分级

P1 — 影响 RPC 层、Client 层、配置契约。
