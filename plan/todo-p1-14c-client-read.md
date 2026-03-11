# Todo: P1-14C Client.Read 最小实现

> 对应 Plan: plan/plan_p1-14c-client-read.md

## Todo 列表

| ID | 任务 | 状态 | DoD |
|----|------|------|-----|
| 1 | MasterClient::GetFileInfo | completed | GetFileInfo RPC 封装完成，单元测试可调用 |
| 2 | ClientConfig.page_size | completed | 新增字段，默认 1MB |
| 3 | FluxCacheClient::Read 主逻辑 | completed | Read 实现 block/page 切分、选 Worker、ReadPages、裁剪、重试 |
| 4 | tests/client/read_test.cpp | completed | 单页、跨页、跨 block 用例通过 |
| 5 | Testing Gate | completed | 构建、ctest -R read、lint 通过 |

## 执行顺序

1 → 2 → 3 → 4 → 5

## 备注

- 测试使用 block_size=2MB、page_size=1MB 的小尺寸配置。
- Read 失败时 RefreshRing 一次后重试。
