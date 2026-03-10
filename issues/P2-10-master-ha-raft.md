# P2-10: HA Journal / Raft 设计 Spike

## 阶段与优先级

Phase I — HA 与弹性 | P1

## 依赖

- [P1-05B](./P1-05b-inode-store-and-tree.md)
- [P1-08B](./P1-08b-sync-from-ufs-and-unmount-safety.md)
- [P1-15D](./P1-15d-write-path-e2e.md)

## 描述

本 issue 不直接进入完整 HA 实现，而是先收敛 Journal 与现有 RocksDB 元数据持久化之间的关系。

Spike 需要回答的核心问题：

- RocksDB 与 Journal 谁是重启恢复的 source of truth
- Journal 是复制通道、审计通道，还是恢复主链路
- 哪些元数据变更必须进入 Journal
- Checkpoint 如何与 `inodes/edges` 协同

## 交付物

- [HA Journal 设计](../design/ha-journal-design.md) 的最终版本
- JournalEntry 最小模型草案
- 关键写路径时序图
- 风险清单与后续实现拆分建议

## 验收标准

- [ ] 明确 RocksDB 与 Journal 的职责边界。
- [ ] 明确哪些 RPC / 元数据变更必须写 Journal。
- [ ] 明确恢复流程，不允许同时存在两个互相竞争的恢复主链路。
- [ ] 若需要实现代码，仅限最小原型或接口草图，不改变现有主路径行为。

## 涉及目录

```text
design/
src/master/ha/
```
