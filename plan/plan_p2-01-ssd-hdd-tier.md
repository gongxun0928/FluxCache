# Plan: P2-01 SSD/HDD Tier 扩展

> Status: Done

## Goal

- **Problem**: 仅有 Memory tier，无法支撑恢复与多层缓存。
- **Target outcome**: 补齐 SSD/HDD 基于本地文件的 tier，实现 TierManager 按优先级选择 tier；各 tier 可独立分配读写，容量统计正确，重启后文件仍存在；单元测试通过。

## 设计依据

- [design/ssd-hdd-tier-design.md](../design/ssd-hdd-tier-design.md)

## Steps

1. 实现 `FileTier` 基类或共用逻辑，SSD/HDD 继承/复用，基于 `std::filesystem`、`std::fstream` 存储 `block-{id}.dat`。
2. 实现 `SsdTier`、`HddTier`（可均为 `FileTier` 的 type alias 或薄封装，区分配置用途）。
3. 实现 `TierManager`：持有多个 tier、按优先级 Allocate、维护 handle→tier 映射，转发 Write/Read/Release。
4. 将 `ssd_tier.cpp`、`hdd_tier.cpp`、`tier_manager.cpp` 加入 `fluxcache_worker`。
5. 实现 `ssd_tier_test.cpp`、`hdd_tier_test.cpp`、`tier_manager_test.cpp`，覆盖分配读写、容量耗尽、持久化、优先级、容量统计。
6. 执行 `cd build && cmake .. && cmake --build . && ctest -R tier -C Debug --output-on-failure`。

## Risks & Assumptions

- **Risk**: 不同平台 `std::filesystem` 行为差异。**Mitigation**: 使用标准 API，测试用 `temp_directory_path()`。
- **Assumption**: 单进程单线程使用，与 MemoryTier 一致。

## To Confirm

- [x] 验收标准以 issues/P2-01-storage-tier-ssd-hdd.md 为准。
- [x] 变更分级 P1，遵循 change-tiering-and-qg.mdc。

## 变更分级

P1 — 新增核心存储组件，影响 Worker 存储层。
