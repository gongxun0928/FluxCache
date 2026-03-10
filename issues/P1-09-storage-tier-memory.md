# P1-09: Memory Tier 基础能力

## 阶段与优先级

Phase C — 数据面闭环 | P1

## 依赖

- [P1-06](./P1-06-worker-skeleton.md)

## 描述

实现 Worker 端的最小存储层抽象与 `Memory` tier。它只负责页级数据块的分配、读写、释放和容量统计，不直接感知分布式 block 路由。

## 交付物

- `src/worker/storage/storage_tier.h`
- `src/worker/storage/memory_tier.h/.cpp`
- `TierBlockHandle`

## 验收标准

- [ ] 可分配指定大小的数据块并写入/读取内容。
- [ ] 容量达到上限时返回明确错误。
- [ ] 释放后容量正确回收。
- [ ] 单元测试覆盖分配、读写、释放、容量耗尽。

## 涉及目录

```text
src/worker/storage/
tests/worker/
```
# P1-09: Memory Tier 基础能力

## 阶段与优先级

Phase C — 数据面闭环 | P1

## 依赖

- [P1-06](./P1-06-worker-skeleton.md)

## 描述

实现 Worker 端的最小存储层抽象与 `Memory` tier。它只负责页级数据块的分配、读写、释放和容量统计，不直接感知分布式 block 路由。

## 交付物

- `src/worker/storage/storage_tier.h`
- `src/worker/storage/memory_tier.h/.cpp`
- `TierBlockHandle`

## 验收标准

- [ ] 可分配指定大小的数据块并写入/读取内容。
- [ ] 容量达到上限时返回明确错误。
- [ ] 释放后容量正确回收。
- [ ] 单元测试覆盖分配、读写、释放、容量耗尽。

## 涉及目录

```text
src/worker/storage/
tests/worker/
```
