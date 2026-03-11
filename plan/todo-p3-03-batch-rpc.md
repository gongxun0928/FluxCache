# Todo: P3-03 批量 RPC 与顺序读 Pipeline

> Status: Done

## 条目清单

| # | 任务 | 状态 | DoD |
|---|------|------|-----|
| 1 | 扩展 proto BatchReadPages | completed | worker.proto 新增 RPC 与消息；重新生成 pb |
| 2 | Worker BatchReadPages 实现 | completed | WorkerServiceImpl::BatchReadPages 遍历 requests 返回 block_data |
| 3 | WorkerClient BatchReadPages | completed | worker_client 新增方法，复用 RetryPolicy |
| 4 | ClientConfig 扩展 | completed | prefetch_blocks、batch_read_max_blocks |
| 5 | Client Read 批量+预读逻辑 | completed | 按 Worker 分组、BatchReadPages、顺序预取 |
| 6 | sequential_read_bench | completed | tests/benchmark/sequential_read_bench.cpp |
| 7 | 构建与测试 | completed | cmake --build . && ctest 通过；基准可运行 |

## 执行顺序

1 → 2 → 3 → 4 → 5 → 6 → 7

## 备注

- 设计见 design/batch-rpc-sequential-read-design.md
- 计划见 plan/plan_p3-03-batch-rpc.md
