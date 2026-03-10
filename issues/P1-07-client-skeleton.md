# P1-07: Client RPC 封装与本地路由缓存骨架

## 阶段与优先级

Phase C — 数据面闭环 | P1

## 依赖

- [P1-02](./P1-02-config-system.md)
- [P1-04](./P1-04-channel-pool-minimal.md)
- [P1-16](./P1-16-core-types.md)

## 描述

实现 Client 侧最小 transport 外壳：Master/Worker stub 封装、deadline 设置、本地 ring 缓存和按 `BlockId` 选择 Worker 的能力。

本 issue 不实现完整读写主路径，这些逻辑由 `P1-14C` 和 `P1-15C` 单独承担。

## 交付物

- `src/client/fluxcache_client.h/.cpp`
- `src/client/master_client.h/.cpp`
- `src/client/worker_client.h/.cpp`
- `src/client/cached_hash_ring.h/.cpp`

## 验收标准

- [ ] 可通过 ChannelPool 创建 Master 与 Worker stub。
- [ ] 所有 RPC 调用都设置 deadline。
- [ ] ring 缓存可根据 `ring_version` 初始化和刷新。
- [ ] 无 Worker、Master 不可达、Worker 不可达时返回稳定错误码。

## 涉及目录

```text
src/client/
tests/client/
```
