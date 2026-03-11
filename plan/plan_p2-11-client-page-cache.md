# Plan: P2-11 Client 本地 Page 内存缓存

> Status: Done

## Goal

在 SDK 内部提供可选的 L1 Page 内存缓存，使用与 Worker PageStore 一致的 mtime 校验机制，实现 Get/Put/Invalidate/InvalidateFile/Clear 与 LRU 淘汰，并通过 SDKConfig.local_cache_enabled 控制开关。

## 设计依据

- [design/client-page-cache-design.md](../design/client-page-cache-design.md)
- [design/client-sdk-design.md](../design/client-sdk-design.md) §3
- [issues/P2-11-client-local-cache.md](../issues/P2-11-client-local-cache.md)

## Steps

1. **ClientConfig 扩展**：增加 `local_cache_enabled`、`local_cache_size_mb`。
2. **SDKConfig 扩展**：增加 `local_cache_enabled`、`local_cache_size_mb`。
3. **client_page_cache.h**：ClientPageCache 类声明，Get/Put/Invalidate/InvalidateFile/Clear。
4. **client_page_cache.cpp**：实现 LRU 淘汰、mtime 校验逻辑。
5. **fluxcache_client**：持有 ClientPageCache，Read 路径集成 L1 查询与回填。
6. **fluxcache_sdk**：Create 时根据 SDKConfig 构造 ClientConfig，传递 local_cache 参数。
7. **client_page_cache_test.cpp**：单元测试覆盖 Get 命中/未命中、mtime 不匹配、LRU 淘汰、Invalidate/InvalidateFile/Clear。
8. **sdk_test 扩展**：集成测试覆盖首次读走 Worker、二次读命中 L1、local_cache_enabled=false。

## 文件路径

| 文件 | 操作 |
|------|------|
| design/client-page-cache-design.md | 新增 |
| plan/plan_p2-11-client-page-cache.md | 新增 |
| src/common/config/config.h | 修改（ClientConfig 扩展） |
| src/client/sdk/types.h | 修改（SDKConfig 扩展） |
| src/client/cache/client_page_cache.h | 新增 |
| src/client/cache/client_page_cache.cpp | 新增 |
| src/client/fluxcache_client.h | 修改（持有 cache，Read 集成） |
| src/client/fluxcache_client.cpp | 修改 |
| src/client/sdk/fluxcache_sdk.cpp | 修改（传递 local_cache 配置） |
| src/client/CMakeLists.txt | 修改（添加 client_page_cache.cpp） |
| tests/client/client_page_cache_test.cpp | 新增 |
| tests/client/CMakeLists.txt | 修改（添加 client_page_cache_test） |
| tests/client/sdk_test.cpp | 修改（L1 命中集成测试） |

## 变更分级

P1 — 新增 Client 缓存模块，影响 SDK 与 Client 读路径。

## 验证动作

- 构建：`cd build && cmake --build .`
- 测试：`cd build && ctest --output-on-failure`
- Lint：对改动文件执行 lint 检查

## Risks

- 集成测试需验证 L1 命中，可通过 RPC 调用计数或 FakeUfs 行为间接验证。
