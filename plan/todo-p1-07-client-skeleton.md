# Todo: P1-07 Client RPC 封装与本地路由缓存骨架

> Status: Done

## 条目清单

| # | 任务 | 状态 | DoD |
|---|------|------|-----|
| 1 | 扩展 Status：新增 kUnavailable、Status::Unavailable() | pending | status.h/cpp 更新，编译通过 |
| 2 | 实现 CachedHashRing | pending | Update/GetVersion/GetWorker/GetWorkerAddress 正确，单元测试覆盖 |
| 3 | 实现 MasterClient | pending | GetHashRing 设置 deadline，错误映射正确 |
| 4 | 实现 WorkerClient | pending | ReadPages/WritePages 骨架，设置 deadline |
| 5 | 实现 FluxCacheClient | pending | 门面整合，GetWorkerForBlock/GetWorkerClient 正确 |
| 6 | 实现 client_test.cpp | pending | 覆盖验收标准：stub 创建、deadline、ring 缓存、错误码 |
| 7 | 构建与测试 | pending | cd build && cmake .. && cmake --build . && ctest -R client -C Debug --output-on-failure 通过 |

## 执行顺序

1 → 2 → 3 → 4 → 5 → 6 → 7

## 备注

- channel_pool_test 已链接 fluxcache_proto 但未链接 fluxcache_common（因 ChannelPool 在 common 中，但 channel_pool.h 只依赖 grpc）。client 模块需链接 fluxcache_common fluxcache_proto。
- CachedHashRing 需解析 proto WorkerEndpoint 为内部结构，哈希逻辑与 HashRingManager 一致。
