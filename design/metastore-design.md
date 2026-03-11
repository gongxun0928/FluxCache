# MetaStore RocksDB 设计

> 关联 Issue: P2-05
> 依赖: P2-01 (SSD/HDD Tier), P1-10 (PageStore)

## 1. 概述

实现 Worker 的持久化页索引（MetaStore），使 Worker 重启后无需从 UFS 全量扫描即可恢复缓存元数据。MetaStore 使用 RocksDB 存储 PageId → PageMeta 映射，支持 Put/Get/Delete/DeleteByBlock/ScanAll/ScanBlock。

## 2. 接口与约束

### 2.1 Key 编码（大端）

- Key: `BlockId(8B) + PageIndex(2B)`，大端编码
- 支持前缀范围扫描：ScanBlock(block_id) 使用 Seek(prefix) + 范围迭代

### 2.2 PageMeta 结构

```cpp
struct PageMeta {
  TierType tier_type;      // 归属 tier (Memory/SSD/HDD)
  uint64_t tier_block_id;  // tier 内块 ID（FileTier 即 block-{id}.dat 的 id）
  int64_t cached_mtime_ms; // Phase 1 版本校验
  // file_version 预留，Phase 1 不启用
};
```

- `tier_type` + `tier_block_id`：用于恢复时校验 tier 文件是否存在
- `cached_mtime_ms`：与 UFS mtime 比对，过期则淘汰

### 2.3 MetaStore 接口

| 方法 | 语义 |
|------|------|
| Put(PageId, PageMeta) | 写入或覆盖 |
| Get(PageId) → optional<PageMeta> | 按 key 查询 |
| Delete(PageId) | 删除单条 |
| DeleteByBlock(BlockId) | 删除该 block 下全部页 |
| ScanAll(callback) | 全表扫描 |
| ScanBlock(block_id, callback) | 范围扫描该 block 下全部页 |

## 3. 写入顺序

采用 **先写数据、再写 MetaStore** 策略：

1. PageStore::PutPage: tier->Allocate → tier->Write → MetaStore->Put
2. 若 MetaStore Put 失败：tier 数据已落盘，恢复时通过 ScanAll 可发现；若 tier 文件存在则恢复，否则清理脏记录
3. 恢复时校验：对每条 MetaStore 记录，检查 tier 文件是否存在；不存在则 Delete 并继续

## 4. 恢复流程

1. 打开 MetaStore RocksDB
2. 创建 TierManager（Memory + SSD + HDD，按 data_dir 配置）
3. ScanAll MetaStore：
   - 对每条 (PageId, PageMeta)：根据 tier_type 取 tier 根路径，检查 `block-{tier_block_id}.dat` 是否存在
   - 存在：TierManager::RegisterRecoveredBlock(tier_type, tier_block_id) 获得 handle，恢复 page_index_
   - 不存在：MetaStore->Delete(PageId)，跳过（清理脏记录）
4. PageStore 从恢复的 page_index_ 正常工作

## 5. Tier 信息获取

- StorageTier 新增 `GetTierType()` 纯虚方法
- StorageTier 新增 `GetBlockTierInfo(handle_id, tier_type*, tier_block_id*)` 默认实现：单 tier 时 `*tier_block_id = handle_id`
- TierManager 重写 `GetBlockTierInfo`：从 handle_to_tier_ 查 tier 与 tier_handle.id
- TierManager 新增 `RegisterRecoveredBlock(tier_type, tier_block_id)`：为已存在的 tier 块创建 handle 映射

## 6. 测试设计

| 用例 | 目标 |
|------|------|
| PutGetDelete | 基本 CRUD |
| DeleteByBlock | 按 block 批量删除 |
| ScanBlock | 范围扫描返回该 block 下全部页 |
| ScanAll | 全表迭代 |
| RecoveryFullFlow | 写入 → 关闭 → 重启 → 恢复 → 可读 |
| RecoveryOrphanCleanup | tier 文件缺失时清理 MetaStore 脏记录并继续启动 |

## 7. 风险与假设

- **风险**：多 tier 混合时 Memory 页恢复会全部清理。**Mitigation**：Memory 为易失层，设计上不持久，符合预期。
- **假设**：WorkerConfig.data_dir 非空时启用 SSD/HDD tier；data_dir 为空时仅 Memory，恢复时 MetaStore 条目全部清理。
