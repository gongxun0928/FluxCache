# P1-10: PageStore 最小实现

## 阶段与优先级

Phase C — 数据面闭环 | P1

## 依赖

- [P1-09](./P1-09-storage-tier-memory.md)
- [P1-16](./P1-16-core-types.md)

## 描述

实现 Worker 侧页级缓存引擎 `PageStore`。Phase 1 只要求：

- `PageId` 到 `TierBlockHandle` 的索引
- `mtime` 校验
- 单机单进程正确性

并发优化与持久化恢复由后续 issue 负责。

## 交付物

- `src/worker/page/page_store.h/.cpp`
- `GetPage`
- `PutPage`
- `DeletePage`
- `DeleteBlockPages`
- `Contains`
- `BlockId -> page_index set` 二级索引

## 验收标准

- [ ] `PutPage` 后可通过 `GetPage` 正确读取。
- [ ] `DeletePage` 后返回 miss。
- [ ] `DeleteBlockPages` 可清理同一 block 下全部页。
- [ ] `expected_mtime` 不匹配时返回 miss 并清理旧页。
- [ ] 测试使用可控 mtime 值，不依赖真实文件系统时间精度。
- [ ] 底层 tier 容量不足时返回明确错误。

## 涉及目录

```text
src/worker/page/
tests/worker/
```
# P1-10: PageStore 最小实现

## 阶段与优先级

Phase C — 数据面闭环 | P1

## 依赖

- [P1-09](./P1-09-storage-tier-memory.md)
- [P1-16](./P1-16-core-types.md)

## 描述

实现 Worker 侧页级缓存引擎 `PageStore`。Phase 1 只要求：

- `PageId` 到 `TierBlockHandle` 的索引
- `mtime` 校验
- 单机单进程正确性

并发优化与持久化恢复由后续 issue 负责。

## 交付物

- `src/worker/page/page_store.h/.cpp`
- `GetPage`
- `PutPage`
- `DeletePage`
- `DeleteBlockPages`
- `Contains`
- `BlockId -> page_index set` 二级索引

## 验收标准

- [ ] `PutPage` 后可通过 `GetPage` 正确读取。
- [ ] `DeletePage` 后返回 miss。
- [ ] `DeleteBlockPages` 可清理同一 block 下全部页。
- [ ] `expected_mtime` 不匹配时返回 miss 并清理旧页。
- [ ] 测试使用可控 mtime 值，不依赖真实文件系统时间精度。
- [ ] 底层 tier 容量不足时返回明确错误。

## 涉及目录

```text
src/worker/page/
tests/worker/
```
