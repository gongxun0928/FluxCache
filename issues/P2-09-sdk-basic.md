# P2-09: C++ SDK MVP

## 阶段与优先级

Phase G — 访问入口 | P1

## 依赖

- [P1-14D](./P1-14d-read-path-e2e.md)
- [P1-15D](./P1-15d-write-path-e2e.md)
- [P2-08](./P2-08-channel-pool-full.md)

## 描述

在 Client 库基础上提供面向应用开发者的 SDK MVP。

为避免协议与能力错位，本 issue 只覆盖当前服务端已稳定建模的文件级主路径：

- `Create`
- `Open`
- `Read`
- `Write`
- `Stat`
- `Delete`

以下能力明确后移，不属于 MVP 验收范围：

- `Rename`
- `List`
- `Mkdir`
- `Rmdir`
- `Exists`

## 交付物

- `src/client/sdk/fluxcache_sdk.h/.cpp`
- `src/client/sdk/file_handle.h/.cpp`
- `src/client/sdk/types.h`
- `examples/sdk_example.cpp`

## 验收标准

- [ ] SDK 头文件不暴露 gRPC、protobuf 等内部依赖。
- [ ] 通过 SDK 可完成文件级主路径：`Create -> Open -> Write -> Read -> Stat -> Delete`。
- [ ] 错误通过 `Status` / `StatusOr<T>` 传递，不抛异常。
- [ ] 示例代码可编译运行。
- [ ] 文档明确写出 MVP 不支持的 namespace API，避免用户误以为已实现。

## 涉及目录

```text
src/client/sdk/
examples/
tests/client/
```
