# Plan: P2-09 C++ SDK MVP

> Status: Done

## Goal

在 FluxCacheClient 之上提供 C++ SDK MVP，完成文件级主路径 Create → Open → Write → Read → Stat → Delete，头文件不暴露 gRPC/protobuf，错误通过 Status/StatusOr 传递。

## 设计依据

- [design/sdk-mvp-design.md](../design/sdk-mvp-design.md)
- [design/client-sdk-design.md](../design/client-sdk-design.md)
- [issues/P2-09-sdk-basic.md](../issues/P2-09-sdk-basic.md)

## Steps

1. **common/status_or.h**：新增 StatusOr 模板，仅依赖 status.h 与 optional；config.h 改为使用之。
2. **MasterClient::DeleteFile**：在 master_client.h/cpp 中实现 DeleteFile(path)，调用 Master RPC。
3. **FluxCacheClient::Delete**：在 fluxcache_client.h/cpp 中实现 Delete(path)，委托 MasterClient::DeleteFile。
4. **src/client/sdk/types.h**：SDKConfig、FileInfo、OpenMode，仅依赖 status.h、status_or.h。
5. **src/client/sdk/file_handle.h/.cpp**：FileHandle 类，PIMPL，封装 Read/Write/Stat/Close。
6. **src/client/sdk/fluxcache_sdk.h/.cpp**：FluxCacheSDK 类，PIMPL，封装 Create/Open/Delete/Stat。
7. **CMake 集成**：client 模块新增 fluxcache_sdk 静态库，examples 目录新增 sdk_example 可执行文件。
8. **examples/sdk_example.cpp**：演示 Create → Open → Write → Read → Stat → Delete 流程。
9. **tests/client/sdk_test.cpp**：单元测试，覆盖主路径与错误路径；可依赖 E2E 环境或 mock。
10. **文档**：在 fluxcache_sdk.h 或 README 中明确写出 MVP 不支持的 Rename/List/Mkdir/Rmdir/Exists。

## 文件路径

| 文件 | 操作 |
|------|------|
| src/common/status_or.h | 新增 |
| src/common/config/config.h | 修改（使用 status_or.h） |
| src/client/master_client.h | 修改（声明 DeleteFile） |
| src/client/master_client.cpp | 修改（实现 DeleteFile） |
| src/client/fluxcache_client.h | 修改（声明 Delete） |
| src/client/fluxcache_client.cpp | 修改（实现 Delete） |
| src/client/sdk/types.h | 新增 |
| src/client/sdk/fluxcache_sdk.h | 新增 |
| src/client/sdk/fluxcache_sdk.cpp | 新增 |
| src/client/sdk/file_handle.h | 新增 |
| src/client/sdk/file_handle.cpp | 新增 |
| examples/sdk_example.cpp | 新增 |
| tests/client/sdk_test.cpp | 新增 |
| src/client/CMakeLists.txt | 修改 |
| CMakeLists.txt 或 examples/CMakeLists.txt | 修改（添加 examples） |

## 变更分级

P1 — 新增公共 SDK 接口，影响 client 模块与构建系统。

## 验证动作

- 构建：`cd build && cmake .. && cmake --build .`
- 测试：`cd build && ctest -R sdk -C Debug --output-on-failure`
- 示例：`./build/examples/sdk_example` 可运行（需配置 master 地址）
- Lint：对改动文件执行 lint 检查

## Risks

- DeleteFile 服务端当前返回 UNIMPLEMENTED，SDK Delete 将返回错误；API 表面完整，后续 Master 实现后即可工作。
