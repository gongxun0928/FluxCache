# Todo: P3-02 PageStore 并发优化

> Plan: [plan_p3-02-pagestore-concurrency.md](./plan_p3-02-pagestore-concurrency.md)

## Todo 列表

| # | 任务 | 状态 | DoD |
|---|------|------|-----|
| 1 | PageStore 两级分段锁重构 | completed | stripe_locks_ 按 BlockId 分片，GetPage/PutPage 等加锁，设计文档一致 |
| 2 | 并发正确性测试 | completed | 多线程 GetPage/PutPage/DeletePage 压力测试，TSAN 无 data race |
| 3 | page_store_bench 基准测试 | completed | 读吞吐、混合读写延迟可执行并输出 |
| 4 | 构建/ctest/TSAN 验证 | completed | page_store_test/memory_tier_test 全通过，TSAN 构建无 race |

## 约束

- 全程最多一个 `in_progress`
- DoD 可验证
