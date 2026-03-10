# P2-01: SSD / HDD Tier 扩展

## 阶段与优先级

Phase D — 恢复与删除语义 | P1

## 依赖

- [P1-09](./P1-09-storage-tier-memory.md)

## 描述

在 `Memory` tier 之外补齐基于本地文件的 `SSD` / `HDD` tier，为恢复和多层缓存演进打基础。

## 交付物

- `src/worker/storage/ssd_tier.h/.cpp`
- `src/worker/storage/hdd_tier.h/.cpp`
- `src/worker/storage/tier_manager.h/.cpp`

## 验收标准

- [ ] SSD/HDD tier 可独立完成分配和读写。
- [ ] TierManager 可按优先级选择 tier。
- [ ] 各 tier 容量统计正确。
- [ ] 重启后磁盘上的 tier 文件仍存在。
- [ ] 单元测试通过。

## 涉及目录

```text
src/worker/storage/
tests/worker/
```
# P2-01: SSD / HDD Tier 扩展

## 阶段与优先级

Phase D — 恢复与删除语义 | P1

## 依赖

- [P1-09](./P1-09-storage-tier-memory.md)

## 描述

在 `Memory` tier 之外补齐基于本地文件的 `SSD` / `HDD` tier，为恢复和多层缓存演进打基础。

## 交付物

- `src/worker/storage/ssd_tier.h/.cpp`
- `src/worker/storage/hdd_tier.h/.cpp`
- `src/worker/storage/tier_manager.h/.cpp`

## 验收标准

- [ ] SSD/HDD tier 可独立完成分配和读写。
- [ ] TierManager 可按优先级选择 tier。
- [ ] 各 tier 容量统计正确。
- [ ] 重启后磁盘上的 tier 文件仍存在。
- [ ] 单元测试通过。

## 涉及目录

```text
src/worker/storage/
tests/worker/
```
