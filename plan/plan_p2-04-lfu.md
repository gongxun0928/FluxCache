# Plan: P2-04 LFU 淘汰策略

> Status: Done

## Goal

- **Problem**: 当前仅有 LRU 策略，需补齐 LFU 以支持可切换淘汰策略。
- **Target outcome**: 实现 LFU 策略；访问频率最低的页优先淘汰，同频按 LRU 顺序；Factory 注册；配置可切换；单元测试通过。

## Design Summary

### LFU 行为

- **PickVictim**: 选取访问频率最低的页；同频时选取最近最少使用（LRU）的页。
- **OnInsert**: 新页频率为 1，插入 freq=1 桶的 MRU 端。
- **OnAccess**: 页从当前 freq 桶移除，加入 freq+1 桶的 MRU 端。
- **OnRemove**: 从索引和对应 freq 桶移除。
- **GetOrderedFromMru**: 返回 MFU 到 LFU 顺序（高 freq 优先，同 freq 内 MRU 到 LRU），供 TierPromoter 选取晋升候选。

### 数据结构

- `std::map<uint64_t, std::list<PageId>>`：freq -> LRU 有序列表（front=MRU, back=LRU）。
- `std::unordered_map<PageId, std::pair<uint64_t, std::list<PageId>::iterator>>`：PageId -> (freq, iterator)。
- PickVictim：取 min_freq 桶的 back()。

### 测试设计

| 用例 | 描述 | 验证点 |
|------|------|--------|
| PickVictim_Empty_ReturnsNullopt | 空策略 | 返回 nullopt |
| PickVictim_SinglePage_ReturnsThatPage | 单页 | 返回该页 |
| PickVictim_LowestFreqFirst | A/B/C 插入，A 访问 2 次，B 访问 1 次 | 淘汰 B（频率最低） |
| PickVictim_SameFreq_LruOrder | A/B 同频，A 更早未访问 | 淘汰 A |
| OnAccess_IncreasesFreq | 插入后多次 OnAccess | 淘汰顺序变化 |
| OnRemove_ExcludesFromEviction | Remove 后 | 该页不再被选 |
| GetOrderedFromMru_MfuToLfu | 多页不同频率 | 高 freq 在前 |
| Factory_CreatesLFU | Factory kLFU | 返回非空且行为正确 |

## Steps

1. 创建 `plan/plan_p2-04-lfu.md`（本文档）。
2. 实现 `src/worker/cache/lfu_policy.h` / `lfu_policy.cpp`。
3. 在 `eviction_policy_factory.cpp` 中注册 `kLFU`。
4. 更新 `src/worker/CMakeLists.txt` 添加 `lfu_policy.cpp`。
5. 实现 `tests/worker/lfu_policy_test.cpp`。
6. 更新 `tests/worker/CMakeLists.txt` 添加 `lfu_policy_test`。
7. 扩展 WorkerConfig：`eviction_policy`（"lru"|"lfu"），WorkerServer 创建策略并传入 PageStore。
8. 执行 Testing Gate：构建、测试、Lint。

## Risks & Assumptions

- **Risk**: LFU 可能产生频率偏斜（历史热点长期占据）。**Mitigation**: Phase 1 不实现衰减，与 LRU 并存供场景选择。
- **Assumption**: 单线程使用，与 LruPolicy 一致。

## To Confirm

- [x] 验收标准以 `issues/P2-04-eviction-policy-lfu.md` 为准。
- [x] 依赖 P2-03、P2-02 已完成。

## 变更分级

P1 — 新增策略实现，影响 EvictionPolicyFactory；配置扩展为 P1 质量门。
