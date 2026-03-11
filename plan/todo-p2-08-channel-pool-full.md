# Todo: P2-08 ChannelPool 完整实现与幂等重试边界

> Status: Done

## 条目清单

| # | 任务 | 状态 | DoD |
|---|------|------|-----|
| 1 | 扩展 ClientConfig、新增 ChannelPoolOptions | completed | config.h 增加 channel_pool_size、retry 字段；ChannelPoolOptions 结构体 |
| 2 | 实现 ChannelPool 多 Channel 池、轮询、Warmup、EvictUnhealthy | completed | GetChannel 轮询返回；Warmup 预创建；EvictUnhealthy 剔除不健康 |
| 3 | 实现 RetryPolicy 与 ExecuteWithRetry | completed | retry_policy.h/.cpp，幂等可重试、非幂等不重试 |
| 4 | 集成 MasterClient、WorkerClient 使用 RetryPolicy | completed | 幂等 RPC 走 ExecuteWithRetry，非幂等直接调用 |
| 5 | 更新 FluxCacheClient 传入 ChannelPoolOptions、RetryPolicy | completed | 从 ClientConfig 解析并传入 |
| 6 | 扩展 channel_pool_test：池大小、轮询、预热、健康检查 | completed | 单元测试覆盖新行为 |
| 7 | 实现 channel_pool_retry_test：伪 transport 验证重试边界 | completed | 幂等读重试成功、非幂等写不重试、重试耗尽 |
| 8 | 构建与测试 | completed | cd build && cmake .. && cmake --build . && ctest -R channel_pool -C Debug --output-on-failure 通过 |

## 执行顺序

1 → 2 → 3 → 4 → 5 → 6 → 7 → 8

## 备注

- 伪 transport：in-process gRPC 服务，内部 fail_count，前 N 次返回 UNAVAILABLE。
- 配置加载需支持新字段；无则默认值。
