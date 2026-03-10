# P2-11: Client 本地 Page 内存缓存

## 阶段与优先级

Phase G — 访问入口 | P1

## 依赖

- [P2-09](./P2-09-sdk-basic.md)
- [P1-16](./P1-16-core-types.md)

## 描述

在 SDK 内部提供可选的 L1 Page 内存缓存，使用与 Worker PageStore 一致的 `mtime` 校验机制。

## 交付物

- `src/client/cache/client_page_cache.h/.cpp`
- `Get`
- `Put`
- `Invalidate`
- `InvalidateFile`
- `Clear`
- LRU 淘汰

## 验收标准

- [ ] 命中且 `mtime` 匹配时直接返回缓存数据。
- [ ] `mtime` 不匹配时返回 miss 并淘汰旧页。
- [ ] 容量超限时按 LRU 淘汰。
- [ ] `local_cache_enabled = false` 时缓存不生效。
- [ ] SDK 集成测试覆盖首次读走 Worker、二次读命中 L1。

## 涉及目录

```text
src/client/cache/
src/client/sdk/
tests/client/
```
# P2-11: Client 本地 Page 内存缓存

## 阶段与优先级

Phase G — 访问入口 | P1

## 依赖

- [P2-09](./P2-09-sdk-basic.md)
- [P1-16](./P1-16-core-types.md)

## 描述

在 SDK 内部提供可选的 L1 Page 内存缓存，使用与 Worker PageStore 一致的 `mtime` 校验机制。

## 交付物

- `src/client/cache/client_page_cache.h/.cpp`
- `Get`
- `Put`
- `Invalidate`
- `InvalidateFile`
- `Clear`
- LRU 淘汰

## 验收标准

- [ ] 命中且 `mtime` 匹配时直接返回缓存数据。
- [ ] `mtime` 不匹配时返回 miss 并淘汰旧页。
- [ ] 容量超限时按 LRU 淘汰。
- [ ] `local_cache_enabled = false` 时缓存不生效。
- [ ] SDK 集成测试覆盖首次读走 Worker、二次读命中 L1。

## 涉及目录

```text
src/client/cache/
src/client/sdk/
tests/client/
```
