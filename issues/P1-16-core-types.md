# P1-16: 核心类型定义

## 阶段与优先级

Phase A — 契约冻结 | P1

## 依赖

- [P1-01](./P1-01-project-scaffolding.md)

## 描述

定义贯穿 Master、Worker、Client 的核心标识类型和辅助函数。

## 交付物

- `src/common/types.h`
- `InodeId`
- `BlockId`
- `PageId`
- `WorkerId`
- `TierType`
- `WorkerState`
- `MakeBlockId`
- `GetInodeId`
- `GetBlockIndex`
- `GetBlockCount`
- `GetBlockLength`
- `MakePageId`
- `GetPagesPerBlock`

## 验收标准

- [ ] `BlockId` 编码/解码往返正确。
- [ ] 最大 `InodeId` 和最大 `BlockIndex` 边界值可正确处理。
- [ ] `PageId` 可稳定用于 `unordered_map`。
- [ ] `WorkerState` 与当前设计中的 `ALIVE / SUSPECT / DEAD` 一致。
- [ ] 单元测试通过。

## 涉及目录

```text
src/common/
tests/common/
```
# P1-16: 核心类型定义

## 阶段与优先级

Phase A — 契约冻结 | P1

## 依赖

- [P1-01](./P1-01-project-scaffolding.md)

## 描述

定义贯穿 Master、Worker、Client 的核心标识类型和辅助函数。

## 交付物

- `src/common/types.h`
- `InodeId`
- `BlockId`
- `PageId`
- `WorkerId`
- `TierType`
- `WorkerState`
- `MakeBlockId`
- `GetInodeId`
- `GetBlockIndex`
- `GetBlockCount`
- `GetBlockLength`
- `MakePageId`
- `GetPagesPerBlock`

## 验收标准

- [ ] `BlockId` 编码/解码往返正确。
- [ ] 最大 `InodeId` 和最大 `BlockIndex` 边界值可正确处理。
- [ ] `PageId` 可稳定用于 `unordered_map`。
- [ ] `WorkerState` 与当前设计中的 `ALIVE / SUSPECT / DEAD` 一致。
- [ ] 单元测试通过。

## 涉及目录

```text
src/common/
tests/common/
```
