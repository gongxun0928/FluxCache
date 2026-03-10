# P2-04: LFU 淘汰策略

## 阶段与优先级

Phase F — 多层缓存与淘汰 | P1

## 依赖

- [P2-03](./P2-03-eviction-policy-lru.md)
- [P2-02](./P2-02-tier-promotion-eviction.md)

## 描述

在统一淘汰策略接口之上实现 `LFU`，用于补齐与 `LRU` 的可切换策略能力。

## 交付物

- `src/worker/cache/lfu_policy.h/.cpp`
- 在 `EvictionPolicyFactory` 中注册 `lfu`

## 验收标准

- [ ] 访问频率最低的页优先被淘汰。
- [ ] 相同频率下按最近最少使用顺序处理。
- [ ] 配置切换到 `lfu` 后系统使用该策略。
- [ ] 单元测试通过。

## 涉及目录

```text
src/worker/cache/
tests/worker/
```
# P2-04: LFU 淘汰策略

## 阶段与优先级

Phase F — 多层缓存与淘汰 | P1

## 依赖

- [P2-03](./P2-03-eviction-policy-lru.md)
- [P2-02](./P2-02-tier-promotion-eviction.md)

## 描述

在统一淘汰策略接口之上实现 `LFU`，用于补齐与 `LRU` 的可切换策略能力。

## 交付物

- `src/worker/cache/lfu_policy.h/.cpp`
- 在 `EvictionPolicyFactory` 中注册 `lfu`

## 验收标准

- [ ] 访问频率最低的页优先被淘汰。
- [ ] 相同频率下按最近最少使用顺序处理。
- [ ] 配置切换到 `lfu` 后系统使用该策略。
- [ ] 单元测试通过。

## 涉及目录

```text
src/worker/cache/
tests/worker/
```
