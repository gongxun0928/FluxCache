# Plan: P3-02 PageStore 并发优化

> Status: Done
> 变更分级: P1
> 设计依据: [design/pagestore-concurrency-design.md](../design/pagestore-concurrency-design.md)

## Goal

优化 PageStore 锁粒度，采用按 BlockId 分片的两级分段锁，提升并发读吞吐与混合读写延迟，并通过 TSAN 验证无数据竞争。

## Steps

1. **重构 PageStore 锁粒度**
   - 在 `page_store.h` 中增加 `kNumStripes`、`stripe_locks_`（`std::array<std::shared_mutex, kNumStripes>`）
   - 增加 `recovery_mu_` 用于 `RecoverFromMetaStore`
   - 实现 `StripeIndex(BlockId)` 辅助函数
   - 为 GetPage、PutPage、DeletePage、DeleteBlockPages、Contains、ContainsBlock、GetPageTier、RelocatePage 按设计加锁
   - RecoverFromMetaStore 使用 recovery_mu_ 或遍历时按 stripe 加锁

2. **实现 page_store_bench.cpp**
   - 创建 `tests/benchmark/` 目录
   - 实现读吞吐基准（多线程 GetPage）
   - 实现混合读写基准（读:写 8:2，统计延迟）
   - 可选：粗粒度锁 baseline 对比（用于验收「明确提升」）

3. **TSAN 验证**
   - 添加并发正确性测试（多线程 GetPage/PutPage/DeletePage 压力）
   - CMake 中支持 `-fsanitize=thread` 构建，运行 ctest 验证

4. **回归验证**
   - 运行既有 `page_store_test` 及集成测试，确保全通过

## Risks

- stripe 数过少可能导致热点；256 为经验值，可后续调优
- RecoverFromMetaStore 若按 stripe 逐条加锁，实现稍复杂；可先用 recovery_mu_ 简化

## To Confirm

- 基准测试是否需与「粗粒度锁 baseline」对比：建议保留，便于验收「明确提升」

## Verification

- [ ] `cd build && cmake --build .` 成功
- [ ] `cd build && ctest --output-on-failure` 全通过
- [ ] TSAN 构建并运行测试无 data race 报告
- [ ] 基准测试可执行并输出读吞吐、混合延迟数据
