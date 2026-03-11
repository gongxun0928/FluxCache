# Todo: P2-05 MetaStore RocksDB 集成与恢复

> Status: In Progress

## Items

### 1. [ ] page_meta.h

- Files: `src/worker/meta/page_meta.h`
- DoD:
  - [ ] PageMeta 结构体（tier_type, tier_block_id, cached_mtime_ms）
  - [ ] EncodePageMetaKey / DecodePageMetaKey（大端 BlockId+PageIndex）
  - [ ] EncodePageMetaValue / DecodePageMetaValue

### 2. [ ] meta_store.h/.cpp

- Files: `src/worker/meta/meta_store.h`, `src/worker/meta/meta_store.cpp`
- DoD:
  - [ ] Open(path), Close(), is_open()
  - [ ] Put, Get, Delete, DeleteByBlock
  - [ ] ScanAll, ScanBlock（范围扫描）

### 3. [ ] StorageTier / TierManager 扩展

- Files: `src/worker/storage/storage_tier.h`, `tier_manager.h/.cpp`, `file_tier.h/.cpp`, `memory_tier.h/.cpp`
- DoD:
  - [ ] StorageTier::GetTierType() 纯虚
  - [ ] StorageTier::GetBlockTierInfo(handle_id, tier_type*, tier_block_id*) 默认实现
  - [ ] TierManager::GetBlockTierInfo 重写
  - [ ] TierManager::RegisterRecoveredBlock(tier_type, tier_block_id) -> handle

### 4. [ ] WorkerConfig 扩展

- Files: `src/common/config/config.h`
- DoD:
  - [ ] metastore_path 或 data_dir 下 metastore 子目录

### 5. [ ] PageStore 集成 MetaStore

- Files: `src/worker/page/page_store.h`, `page_store.cpp`
- DoD:
  - [ ] 构造接受 MetaStore* 和 StorageTier*
  - [ ] PutPage 成功后 MetaStore->Put
  - [ ] DeletePage / DeleteBlockPages 后 MetaStore->Delete / DeleteByBlock

### 6. [ ] WorkerServer 启动恢复

- Files: `src/worker/worker_server.h`, `worker_server.cpp`
- DoD:
  - [ ] 创建 MetaStore，Open(metastore_path)
  - [ ] 创建 TierManager（Memory + SSD 当 data_dir 非空）
  - [ ] ScanAll → 校验 tier 文件 → RegisterRecoveredBlock 或 Delete
  - [ ] 恢复 page_index_ 后创建 PageStore

### 7. [ ] meta_store_test.cpp

- Files: `tests/worker/meta_store_test.cpp`
- DoD:
  - [ ] PutGetDelete, DeleteByBlock, ScanBlock, ScanAll
  - [ ] 写入 → 关闭 → 重启 → 恢复 → 可读
  - [ ] tier 文件缺失时清理脏记录
