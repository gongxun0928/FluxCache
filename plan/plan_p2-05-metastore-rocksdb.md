# Plan: P2-05 MetaStore RocksDB 集成与恢复

> Status: Done

## Goal

- **Problem**: Worker 重启后页索引丢失，需从 UFS 全量扫描恢复。
- **Target outcome**: MetaStore RocksDB 持久化页索引，Worker 启动时恢复；tier 文件缺失时清理脏记录并继续启动；PutPage/DeletePage 与 MetaStore 同步。

## Steps

1. 实现 `src/worker/meta/page_meta.h` — PageMeta 结构与大端编解码
2. 实现 `src/worker/meta/meta_store.h/.cpp` — RocksDB 封装，Put/Get/Delete/DeleteByBlock/ScanAll/ScanBlock
3. 扩展 StorageTier：GetTierType()、GetBlockTierInfo()；TierManager：RegisterRecoveredBlock()
4. 扩展 WorkerConfig：metastore_path（或复用 data_dir/metastore）
5. PageStore 集成 MetaStore：PutPage/DeletePage/DeleteBlockPages 同步更新 MetaStore
6. WorkerServer 启动恢复流程：打开 MetaStore → ScanAll → 校验 tier 文件 → 恢复或清理
7. 实现 `tests/worker/meta_store_test.cpp`，覆盖完整恢复流程

## Risks & Assumptions

- **Risk**: TierManager handle 映射与 FileTier block id 不一致。**Mitigation**: PageMeta 存 (tier_type, tier_block_id)，恢复时用 RegisterRecoveredBlock 重建。
- **Assumption**: data_dir 非空时启用 SSD tier；WorkerConfig 增加 metastore_path 或默认 data_dir/metastore。

## To Confirm

- [x] 设计参考 design/metastore-design.md
- [x] 验收标准以 issues/P2-05-metastore-rocksdb.md 为准

## 变更分级

P1 — 涉及持久化格式、Worker 启动流程、PageStore 核心路径。
