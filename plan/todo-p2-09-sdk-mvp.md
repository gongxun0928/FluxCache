# Todo: P2-09 C++ SDK MVP

> 关联 Plan: [plan_p2-09-sdk-mvp.md](./plan_p2-09-sdk-mvp.md)

## Todo 列表

| ID | 内容 | 状态 | DoD |
|----|------|------|-----|
| 1 | 新增 common/status_or.h，config.h 使用之 | pending | StatusOr 可被 config 与 SDK 共用，编译通过 |
| 2 | MasterClient::DeleteFile、FluxCacheClient::Delete | pending | Delete RPC 可调用，FluxCacheClient::Delete 委托之 |
| 3 | sdk/types.h：SDKConfig、FileInfo、OpenMode | pending | 头文件无 gRPC/protobuf 依赖 |
| 4 | sdk/file_handle.h/.cpp | pending | FileHandle 支持 Read/Write/Stat/Close，PIMPL |
| 5 | sdk/fluxcache_sdk.h/.cpp | pending | FluxCacheSDK 支持 Create/Open/Delete/Stat，PIMPL |
| 6 | CMake 集成 sdk 库与 examples | pending | fluxcache_sdk 库、sdk_example 可执行文件可构建 |
| 7 | examples/sdk_example.cpp | pending | 演示完整主路径，可编译运行 |
| 8 | tests/client/sdk_test.cpp | pending | 主路径与错误路径测试通过 |
| 9 | 文档：MVP 不支持的 API | pending | fluxcache_sdk.h 或 README 明确写出 Rename/List/Mkdir/Rmdir/Exists 未支持 |

## 执行顺序

1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9

## 当前 in_progress

无（待开始）
