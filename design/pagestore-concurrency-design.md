# PageStore 并发优化设计

> 日期: 2026-03-11
> 状态: P3-02 设计
> 参考: [metadata-design.md](./metadata-design.md) §2.1.5 两级分段锁

## 1. 概述

优化 `PageStore` 的锁粒度与并发吞吐。当前实现中 `page_index_` 与 `block_to_pages_` 无锁保护，在 gRPC 多线程调用下存在数据竞争风险。采用**按 BlockId 分片的两级分段锁**，在保证正确性的前提下提升并发读吞吐、降低混合读写延迟。

## 2. 现状分析

### 2.1 当前数据结构

- `page_index_`: `unordered_map<PageId, PageEntry>` — 主索引
- `block_to_pages_`: `unordered_map<BlockId, set<uint16_t>>` — Block 到 Page 的二级索引
- `gc_mu_`: 仅保护 `gc_scan_cursor_`，不保护上述索引

### 2.2 调用路径

- `WorkerServiceImpl::ReadPages` / `WritePages` → `PageStore::GetPage` / `PutPage`（gRPC 多线程并发）
- `TierEvictor` / `TierPromoter` → `GetPageTier`（后台线程）
- `ReconcileGc` → `DeleteBlockPages`（心跳线程）

### 2.3 问题

- 无锁访问 `page_index_` / `block_to_pages_` 导致数据竞争
- 若未来加粗粒度全局锁，则所有操作串行化，读吞吐受限

## 3. 设计方案：按 BlockId 分片的两级分段锁

### 3.1 分片策略

- **分片键**：`BlockId`（PageId.block_id）
- **分片数**：`kNumStripes = 256`（2 的幂，便于取模）
- ** stripe 选择**：`block_id % kNumStripes`

同一 Block 内所有 Page 共享同一 stripe，`DeleteBlockPages` 只需锁一个 stripe。

### 3.2 锁类型

- **stripe 锁**：`std::shared_mutex`（读写锁）
- **全局恢复锁**：`std::mutex` — 仅用于 `RecoverFromMetaStore`，保证恢复期间独占

### 3.3 操作锁策略

| 操作 | 锁类型 | 说明 |
|------|--------|------|
| GetPage | shared(stripe) | 读 page_index_，读后释放再调用 tier_->Read |
| PutPage | unique(stripe) | 写 page_index_、block_to_pages_ |
| DeletePage | unique(stripe) | 写两个索引 |
| DeleteBlockPages | unique(stripe) | 同一 block 全在同一 stripe |
| Contains / ContainsBlock / GetPageTier | shared(stripe) | 只读 |
| RelocatePage | unique(stripe) | 读+写 |
| RecoverFromMetaStore | unique(global_recovery) + unique(all stripes) | 启动时单线程，可简化为全局锁 |
| ReconcileGc | 无额外锁 | 内部调用 DeleteBlockPages，由 stripe 锁保护 |

### 3.4 实现要点

1. **GetPage 锁范围**：持有 shared_lock 期间完成 find + mtime 校验；若需 DeletePage，先释放 shared 再获取 unique 执行删除。
2. **tier_->Read/Write**：在锁外执行，避免持锁做 I/O。
3. **RecoverFromMetaStore**：遍历 MetaStore 时对每个 PageId 加 unique(stripe)，或使用全局锁简化（恢复为启动阶段，性能非关键）。

### 3.5 与 InodeTree 的差异

InodeTree 有 `global_mu_` + `stripe_locks_`，Rename 需 unique(global)。PageStore 无跨 stripe 的复合操作，无需 global_mu_，仅 stripe 锁即可。

## 4. 测试设计

### 4.1 正确性

- **并发正确性**：多线程并发 GetPage/PutPage/DeletePage，无数据竞争（TSAN 通过）
- **回归**：既有 `page_store_test` 全通过

### 4.2 基准测试

- **读吞吐**：N 线程并发 GetPage，对比粗粒度锁（baseline）与分片锁的 ops/s
- **混合读写**：读:写 = 8:2，测量 P50/P99 延迟

### 4.3 验收标准（对齐 issue）

- 并发读吞吐相比粗粒度锁有明确提升
- 混合读写延迟有可量化改善
- 无数据竞争（TSAN clean）
- 既有功能回归测试通过

## 5. 风险与假设

- **假设**：BlockId 在负载下分布较均匀，stripe 间竞争低
- **风险**：单 Block 多 Page 并发访问仍竞争同一 stripe；可接受，因同 Block 访问往往相关
- **MemoryTier**：作为 PageStore 测试与基准的默认后端，已增加 mutex 保证 Allocate/Read/Write/Release 线程安全，满足 TSAN 验证
- **非目标**：meta_store_/eviction_policy_ 的并发安全由外部保证

## 6. 相关文件

| 文件 | 说明 |
|------|------|
| `src/worker/page/page_store.h` | 锁声明、stripe 数组 |
| `src/worker/page/page_store.cpp` | 锁实现 |
| `tests/worker/page_store_test.cpp` | 回归测试 |
| `tests/benchmark/page_store_bench.cpp` | 读写基准测试 |
