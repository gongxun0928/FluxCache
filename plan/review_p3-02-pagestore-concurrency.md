# Review: P3-02 PageStore 并发优化

> 日期: 2026-03-11
> 变更分级: P1

## 结论

**通过**

## 风险等级

低

## 发现列表

无阻断性问题。

## 改动说明

1. **PageStore 两级分段锁**：按 BlockId 分片 256 个 stripe，`recovery_mu_` + `stripe_locks_`，GetPage 用 shared_lock，PutPage/DeletePage 用 unique_lock，I/O 在锁外执行。
2. **MemoryTier 线程安全**：增加 `mu_` 保护 Allocate/Read/Write/Release，满足 TSAN 要求。
3. **page_store_bench**：读吞吐（8 线程）、混合读写 8:2 延迟基准。
4. **ConcurrentGetPutDeleteStress**：多线程 GetPage/PutPage/DeletePage 压力测试。
5. **FLUXCACHE_ENABLE_TSAN**：CMake 选项支持 ThreadSanitizer 构建。

## 测试执行记录

| 测试 | 结果 |
|------|------|
| page_store_test (7 tests) | PASSED |
| memory_tier_test (6 tests) | PASSED |
| read_pages_test (6 tests) | PASSED |
| write_pages_test (5 tests) | PASSED |
| gc_reconciliation_test (2 tests) | PASSED |
| page_store_bench | 读吞吐 ~1.5M ops/s, P50=6us, P99=210us |
| TSAN (build-tsan) | page_store_test 无 data race |

## 残余风险与测试缺口

- 粗粒度锁 baseline 对比未实现，验收「明确提升」依赖基准绝对值（~1.5M ops/s 读吞吐）。
- client_test 构建失败为既有问题，非本次改动引入。
