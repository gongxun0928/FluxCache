# Todo: P3-04 零拷贝

> 关联: plan/plan_p3-04-zero-copy.md

## Todo 列表

| ID | 内容 | 状态 | DoD |
|----|------|------|-----|
| T1 | 实现 Worker ReadPages 单页快速路径 | in_progress | 单页时直接 move page_data 到 response，多页逻辑不变 |
| T2 | 实现 BatchReadPages 单页 per-request 快速路径 | pending | 每个单页 request 走等效优化 |
| T3 | 新增/扩展单页读 benchmark | pending | 可运行并输出优化前后数据 |
| T4 | 功能回归 + 基准验证 | pending | ctest 通过，benchmark 有对比输出 |

## DoD 说明

- **T1**：`worker_service_impl.cpp` 中 ReadPages 在 `page_indices().size()==1` 时跳过 concatenated，直接 set_data(move(page_data))
- **T2**：BatchReadPages 循环内对单页 request 做同样处理
- **T3**：benchmark 覆盖单页读场景，打印 throughput/latency
- **T4**：所有现有测试通过，无功能回归
