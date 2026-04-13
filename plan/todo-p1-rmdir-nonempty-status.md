# Todo: Rmdir Non-Empty Status

> 关联 Plan: [plan_rmdir-nonempty-status.md](./plan_rmdir-nonempty-status.md)

## Todo 列表

| ID | 内容 | 状态 | DoD |
|---|---|---|---|
| 1 | 新增回归测试复现非空目录 `Rmdir` 状态错误 | done | 测试在修复前失败，失败原因与状态映射不符直接相关 |
| 2 | 新增 `kDirectoryNotEmpty` 状态码与工厂方法 | done | `Status` 可构造该状态，相关测试通过 |
| 3 | 修复 `MasterClient::Rmdir` 的 gRPC 状态映射 | done | `FAILED_PRECONDITION` 不再退化为 `IOError` |
| 4 | 修复 FUSE errno 映射 | done | `kDirectoryNotEmpty` 映射到 `-ENOTEMPTY` |
| 5 | 执行构建、测试、lint | done | 按 Testing Gate 完成构建、定向测试与格式检查；全量 `ctest` 被存量 `metrics_test` 阻塞，已记录证据 |
| 6 | 完成 review 与提交流程 | done | 证据包完整，代码已提交推送并同步 PR |

## 执行顺序

1 → 2 → 3 → 4 → 5 → 6
