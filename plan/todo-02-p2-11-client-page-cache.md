# Todo: P2-11 Client 本地 Page 内存缓存

> 关联 Plan: [plan_p2-11-client-page-cache.md](./plan_p2-11-client-page-cache.md)

## 状态

| # | 任务 | 状态 | DoD |
|---|------|------|-----|
| 1 | ClientConfig / SDKConfig 扩展 | completed | local_cache_enabled, local_cache_size_mb 字段存在 |
| 2 | client_page_cache.h/.cpp | completed | Get/Put/Invalidate/InvalidateFile/Clear 实现，LRU 淘汰 |
| 3 | FluxCacheClient 集成 | completed | Read 路径先查 L1，miss 回填 |
| 4 | FluxCacheSDK Create 传递配置 | completed | SDKConfig → ClientConfig 传递 local_cache 参数 |
| 5 | client_page_cache_test.cpp | completed | 单元测试覆盖验收标准 |
| 6 | sdk_test L1 集成测试 | completed | 首次读 Worker、二次读 L1、local_cache_enabled=false |

## DoD 说明

- **client_page_cache**：Get 命中且 mtime 匹配返回数据；mtime 不匹配淘汰并返回 nullptr；容量超限 LRU 淘汰。
- **集成测试**：Create 文件 → Write → Read 两次相同区间，验证第二次不触发额外 RPC（或通过 mock 计数）。
