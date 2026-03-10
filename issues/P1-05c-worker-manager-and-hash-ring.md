# P1-05C: WorkerManager 与 HashRingManager 最小实现

## 阶段与优先级

Phase B — 元数据面闭环 | P1

## 依赖

- [P1-05A](./P1-05a-master-server-bootstrap.md)
- [P1-16](./P1-16-core-types.md)

## 描述

实现 Master 侧的最小 Worker 注册、状态跟踪与 Block 路由能力。

本 issue 只解决单 Master 下的 Worker 列表、`ALIVE/SUSPECT/DEAD` 状态机、最小一致性哈希，以及 `ring_version` 递增，不负责高级 rebalance 与恢复策略。

## 交付物

- `src/master/worker_manager.h/.cpp`
- `src/master/hash_ring_manager.h/.cpp`
- Worker 注册
- 心跳更新时间维护
- 超时进入 `SUSPECT`、宽限期后进入 `DEAD`
- `GetWorker` / `GetCandidates` / `GetRingSnapshot`

## 验收标准

- [ ] 注册一个 Worker 后，`GetHashRing` 返回 `workers.size() == 1`。
- [ ] 增加或移除 Worker 时 `ring_version` 递增。
- [ ] 相同 `BlockId` 在拓扑不变时返回确定性目标 Worker。
- [ ] `SUSPECT` Worker 仍保留在 ring 数据中，但候选路由默认跳过。
- [ ] 状态机通过可控时钟或注入超时参数验证，而非依赖真实长时间等待。

## 涉及目录

```text
src/master/
tests/master/
```
