# Review: P3-03 批量 RPC 与顺序读 Pipeline

> 日期: 2026-03-11
> 变更分级: P1

## 结论

**通过** | 风险等级: 低

## 发现列表

| 严重度 | 发现 | 建议动作 |
|--------|------|----------|
| 低 | BatchReadPages 每 request 创建新 UFS 实例 | 可后续优化：同 ufs_uri 时复用 |
| 低 | 预读使用 std::async 可能产生较多后台任务 | 可后续增加 prefetch 队列限流 |

## 改动说明

- **Proto**: 新增 BatchReadPages RPC、BatchReadPagesRequest/Response
- **Worker**: 实现 BatchReadPages，遍历 requests 调用现有 PageStore+UFS 逻辑
- **WorkerClient**: 新增 BatchReadPages，复用 RetryPolicy
- **ClientConfig**: prefetch_blocks、batch_read_max_blocks
- **FluxCacheClient::Read**: 按 Worker 分组、批量 RPC、顺序读预取
- **Benchmark**: tests/benchmark/sequential_read_bench.cpp

## 测试执行记录

- read_test: BatchReadReturnsCorrectContent 通过
- read_pages_test: BatchReadPagesReturnsOrderedBlockData 通过
- read_path_e2e_test: 通过
- sequential_read_bench: 通过

## 残余风险

- client_test 因 MasterClient/WorkerClient 构造函数变更失败（非本次改动引入）
