# P2-05B: Worker GC 与 orphan / misplaced block 对账

## 阶段与优先级

Phase D — 恢复与删除语义 | P1

## 依赖

- [P2-05](./P2-05-metastore-rocksdb.md)
- [P1-05C](./P1-05c-worker-manager-and-hash-ring.md)
- [P1-15D](./P1-15d-write-path-e2e.md)

## 描述

在 MetaStore 可恢复页索引之后，实现 Worker 的最小对账 GC：清理已删除文件的 orphan block，以及拓扑变化后已不再归属本 Worker 的 misplaced block。

## 交付物

- Heartbeat 中对账请求与响应字段的真实使用
- orphan inode 清理
- misplaced block 清理
- 分桶或分页遍历 MetaStore，避免一次全量扫描

## 验收标准

- [ ] 删除文件后，对账可清理对应缓存页。
- [ ] 拓扑变化后，对账可清理不再归属当前 Worker 的 block。
- [ ] 对账过程不会阻塞正常读写主路径。
- [ ] 测试使用可控 ring 变化和假 MetaStore 数据，而非依赖长时间后台线程偶发触发。

## 涉及目录

```text
src/master/
src/worker/
tests/integration/
```
