# 层级晋升与淘汰流程设计

> 关联 Issue: P2-02
> 依赖: P2-01 (SSD/HDD Tier), P2-03 (EvictionPolicy), P2-05 (MetaStore)

## 1. 概述

实现多层缓存下的晋升（Promotion）与淘汰（Eviction）执行流程。本设计聚焦「何时搬迁、怎样更新索引、如何不破坏主路径」，不涉及策略定义、恢复闭环和传输优化。

## 2. 目标与范围

### 2.1 目标

- 热页可从低速层（SSD/HDD）晋升到高速层（Memory）
- 容量超阈值时，冷页按策略被降级（Demotion）或驱逐（Eviction）
- 搬迁前后 PageStore / MetaStore 索引保持一致
- 可配置的后台巡检（Phase 1 通过直接触发接口验证）

### 2.2 范围

- **在范围内**：TierPromoter、TierEvictor、PageStore 扩展、TierManager 扩展、EvictionPolicy 扩展
- **不在范围内**：后台线程实现（仅预留配置）、LFU 策略、传输优化

## 3. 接口设计

### 3.1 TierPromoter

```cpp
// src/worker/cache/tier_promoter.h
class TierPromoter {
 public:
  TierPromoter(PageStore* page_store, TierManager* tier_manager,
               MetaStore* meta_store, EvictionPolicy* eviction_policy);

  // 尝试晋升一页：从 EvictionPolicy 的 MRU 端找第一个在低速层的页，搬迁到高速层。
  // 返回 OK 表示成功晋升一页；NotFound 表示无可晋升候选；其他为错误。
  Status PromoteOne();
};
```

**晋升逻辑**：

1. 从 EvictionPolicy 获取 MRU 到 LRU 有序的页列表
2. 遍历，找到第一个当前在 SSD 或 HDD 的页
3. 从源 tier 读取数据 → 在目标 tier（Memory）分配 → 写入 → 更新 page_index_、MetaStore → 释放源块

### 3.2 TierEvictor

```cpp
// src/worker/cache/tier_evictor.h
class TierEvictor {
 public:
  TierEvictor(PageStore* page_store, TierManager* tier_manager,
              MetaStore* meta_store, EvictionPolicy* eviction_policy,
              double high_watermark = 0.9);

  // 当 used/total >= high_watermark 时，选取 victim 并降级或驱逐。
  // 返回 OK 表示成功处理一页；NotFound 表示无可淘汰；其他为错误。
  Status EvictOne();
};
```

**淘汰逻辑**：

1. 若 `UsedCapacity() / CapacityLimit() < high_watermark`，直接返回 OK（无需淘汰）
2. `victim = EvictionPolicy::PickVictim()`，若无则返回 NotFound
3. 获取 victim 当前 tier：
   - Memory → 尝试降级到 SSD；若 SSD 满则驱逐
   - SSD → 尝试降级到 HDD；若 HDD 满或不存在则驱逐
   - HDD → 直接驱逐
4. 降级：读源 → 在目标 tier 分配 → 写 → 更新索引 → 释放源
5. 驱逐：调用 PageStore::DeletePage，EvictionPolicy::OnRemove 由 PageStore 集成时调用

### 3.3 EvictionPolicy 扩展

```cpp
// eviction_policy.h 新增
virtual std::vector<PageId> GetOrderedFromMru() const;
```

- LruPolicy 实现：返回 `order_` 从 front 到 back 的 PageId 列表（MRU 到 LRU）
- 用于 TierPromoter 选取晋升候选

### 3.4 PageStore 扩展

```cpp
// page_store.h 新增
std::optional<TierType> GetPageTier(PageId id) const;

// 内部搬迁：读当前 handle，在 target_tier 分配并写入，更新索引与 MetaStore，释放旧 handle。
// 要求 tier 为 TierManager。
Status RelocatePage(PageId id, TierType target_tier);
```

### 3.5 TierManager 扩展

```cpp
// tier_manager.h 新增
// 在指定 tier 分配。若该 tier 无容量则返回 ResourceExhausted。
Status AllocateInTier(TierType tier_type, size_t size, TierBlockHandle* handle);

// 获取指定类型的 StorageTier*，若不存在返回 nullptr。
StorageTier* GetTier(TierType tier_type) const;
```

## 4. 索引联动

### 4.1 晋升流程

1. `RelocatePage(id, TierType::kMemory)`
2. 内部：读源 → AllocateInTier(Memory) → Write → `page_index_[id] = new_entry` → SyncMetaPut → Release(源)

### 4.2 降级流程

1. `RelocatePage(id, TierType::kSSD)` 或 `RelocatePage(id, TierType::kHDD)`
2. 同上，目标 tier 为 SSD 或 HDD

### 4.3 驱逐流程

1. `DeletePage(id)`：Release、RemoveFromBlockIndex、page_index_.erase、SyncMetaDelete
2. EvictionPolicy::OnRemove 需在 DeletePage 时调用（见下节）

## 5. PageStore 与 EvictionPolicy 集成

当前 PageStore 未集成 EvictionPolicy。为支持淘汰时正确更新策略状态，需：

- PageStore 持有 `EvictionPolicy* eviction_policy_`（可选）
- `PutPage` 成功后：`if (eviction_policy_) eviction_policy_->OnInsert(id)`
- `GetPage` 命中后：`if (eviction_policy_) eviction_policy_->OnAccess(id)`
- `DeletePage` / `DeleteBlockPages` 后：`if (eviction_policy_) eviction_policy_->OnRemove(id)`
- `RelocatePage`：不调用 OnRemove/OnInsert，页仍在缓存中，仅存储位置变化

## 6. 可配置项

```cpp
// WorkerConfig 或 TierPromoterConfig / TierEvictorConfig
struct TierPromotionConfig {
  bool enabled = true;
  // 预留：scan_interval_ms 供后续后台巡检使用
};

struct TierEvictionConfig {
  bool enabled = true;
  double high_watermark = 0.9;  // used/total >= 此值时触发淘汰
};
```

Phase 1 可在构造 TierPromoter/TierEvictor 时直接传入参数，暂不扩展 WorkerConfig。

## 7. 测试设计

| 用例 | 描述 | 验证点 |
|------|------|--------|
| PromoteOne_HotPageInSsd_MovesToMemory | 页在 SSD，触发 PromoteOne | 页迁至 Memory，GetPage 可读，MetaStore 一致 |
| PromoteOne_AllInMemory_ReturnsNotFound | 全部页已在 Memory | PromoteOne 返回 NotFound |
| EvictOne_OverThreshold_DemotesMemoryToSsd | 超阈值，victim 在 Memory | 页迁至 SSD，容量释放 |
| EvictOne_OverThreshold_EvictsWhenSsdFull | SSD 满，victim 在 Memory | 页被驱逐，DeletePage 语义 |
| RelocatePage_IndexConsistency | 搬迁前后 | page_index_、block_to_pages_、MetaStore 一致 |
| DirectTrigger_NoBackgroundThread | 测试直接调用 PromoteOne/EvictOne | 不依赖后台线程，可重复验证 |

测试使用小容量 Memory/SSD、伪数据，直接调用 `PromoteOne()` / `EvictOne()` 验证。

## 8. 风险与假设

- **风险**：RelocatePage 与主路径 PutPage/GetPage 并发时需加锁。**Mitigation**：Phase 1 单线程调用假设，与现有 PageStore 一致。
- **假设**：TierManager 至少包含 Memory；若有 SSD/HDD 则按 [Memory, SSD, HDD] 顺序。
- **假设**：EvictionPolicy 与 PageStore 的页集合一致，即所有 PutPage 都触发了 OnInsert。
