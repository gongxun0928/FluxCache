# Plan: Rmdir Non-Empty Status

> Status: Done

## Goal

修复 `Rmdir` 在目录非空场景下的错误语义丢失问题，使客户端不再把 gRPC `FAILED_PRECONDITION` 退化为 `IOError`，并让 FUSE 返回 `ENOTEMPTY`。

## 设计依据

- [design/rmdir-nonempty-status-design.md](../design/rmdir-nonempty-status-design.md)

## 变更分级

P1 — 涉及客户端错误语义与 FUSE 对外 errno 行为，属于公共接口行为修复。

## Steps

1. 补充回归测试，先复现“非空目录 `Rmdir` 被映射成错误状态”的现象；
2. 在 `src/common/status.h/.cpp` 中新增精确状态码 `kDirectoryNotEmpty`；
3. 修改 `src/client/master_client.cpp`，将 `Rmdir` 的 gRPC `FAILED_PRECONDITION` 映射为 `Status::DirectoryNotEmpty()`；
4. 修改 `src/client/fuse/fuse_ops.cpp`，将 `kDirectoryNotEmpty` 映射为 `-ENOTEMPTY`；
5. 更新必要的状态/单元测试，验证状态工厂与回归路径；
6. 执行构建、测试、lint，并整理 review 证据。

## 文件路径

| 文件 | 操作 |
|---|---|
| design/rmdir-nonempty-status-design.md | 新增 |
| plan/plan_rmdir-nonempty-status.md | 新增 |
| plan/todo-p1-rmdir-nonempty-status.md | 新增 |
| src/common/status.h | 修改 |
| src/common/status.cpp | 修改 |
| src/client/master_client.cpp | 修改 |
| src/client/fuse/fuse_ops.cpp | 修改 |
| tests/client/sdk_test.cpp | 修改 |
| tests/common/status_test.cpp | 修改 |

## 验证动作

- 构建：`cd build && cmake --build .`
- 测试：`cd build && ctest --output-on-failure`
- Lint：`clang-format -style=Google -n --Werror` 检查所有改动的 C++ 文件

## Risks

- 新增状态码会扩展 `StatusCode` 枚举，需避免破坏现有序列化/断言；
- FUSE 的 errno 映射只应对目录非空生效，避免误把泛化前置条件失败都映射成 `ENOTEMPTY`。
