# P2-03: 淘汰策略接口与 LRU 实现

## 阶段与优先级

Phase F — 多层缓存与淘汰 | P1

## 依赖

- [P1-10](./P1-10-page-store.md)

## 描述

定义可插拔淘汰策略接口，并实现 `LRU` 作为第一种策略。

## 交付物

- `src/worker/cache/eviction_policy.h`
- `src/worker/cache/lru_policy.h/.cpp`
- `src/worker/cache/eviction_policy_factory.h/.cpp`

## 验收标准

- [ ] 连续访问后，最久未访问页优先被淘汰。
- [ ] `OnAccess` 后淘汰顺序会正确变化。
- [ ] 接口可被 LFU 等后续策略复用。
- [ ] 单元测试通过。

## 涉及目录

```text
src/worker/cache/
tests/worker/
```
# P2-03: 淘汰策略接口与 LRU 实现

## 阶段与优先级

Phase F — 多层缓存与淘汰 | P1

## 依赖

- [P1-10](./P1-10-page-store.md)

## 描述

定义可插拔淘汰策略接口，并实现 `LRU` 作为第一种策略。

## 交付物

- `src/worker/cache/eviction_policy.h`
- `src/worker/cache/lru_policy.h/.cpp`
- `src/worker/cache/eviction_policy_factory.h/.cpp`

## 验收标准

- [ ] 连续访问后，最久未访问页优先被淘汰。
- [ ] `OnAccess` 后淘汰顺序会正确变化。
- [ ] 接口可被 LFU 等后续策略复用。
- [ ] 单元测试通过。

## 涉及目录

```text
src/worker/cache/
tests/worker/
```
