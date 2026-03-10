# P2-05: MetaStore RocksDB 集成与恢复

## 阶段与优先级

Phase D — 恢复与删除语义 | P1

## 依赖

- [P2-01](./P2-01-storage-tier-ssd-hdd.md)
- [P1-10](./P1-10-page-store.md)

## 描述

实现 Worker 的持久化页索引，使 Worker 重启后无需重新从 UFS 扫描全部数据即可恢复缓存元数据。

本 issue 聚焦“页索引恢复”本身；删除文件与拓扑漂移后的对账清理由 `P2-05B` 单独承担。

## 交付物

- `src/worker/meta/meta_store.h/.cpp`
- `src/worker/meta/page_meta.h`
- 大端编码 key：`BlockId(8B) + PageIndex(2B)`
- `Put`
- `Get`
- `Delete`
- `DeleteByBlock`
- `ScanAll`
- `ScanBlock`
- Worker 启动恢复流程

## 验收标准

- [ ] `PutPage` / `DeletePage` 与 MetaStore 更新保持同步。
- [ ] Worker 重启后可从 MetaStore 恢复页索引。
- [ ] tier 文件缺失时，可清理脏 MetaStore 记录并继续启动。
- [ ] `ScanBlock(block_id)` 可按范围扫描返回该 block 下全部页。
- [ ] 测试覆盖“写入 -> 关闭 -> 重启 -> 恢复 -> 可读”的完整流程。

## 技术要点

- Key 排序必须支持前缀范围扫描。
- Phase 1 版本校验仍使用 `cached_mtime_ms`，`file_version` 只保留字段不启用。
- 写入顺序需明确：允许采用“先写数据、再写 MetaStore”或“先写 MetaStore、恢复时校验清理”中的一种，但必须在实现说明和测试中说清楚。

## 涉及目录

```text
src/worker/meta/
src/worker/page/
tests/worker/
```
