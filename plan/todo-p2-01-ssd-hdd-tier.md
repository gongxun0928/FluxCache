# Todo: P2-01 SSD/HDD Tier 扩展

> 关联 Plan: [plan_p2-01-ssd-hdd-tier.md](./plan_p2-01-ssd-hdd-tier.md)

## Todo 列表

| ID | 内容 | 状态 | DoD |
|----|------|------|-----|
| T1 | 实现 FileTier（file_tier.h/.cpp） | completed | 基于本地文件实现 StorageTier，支持恢复 |
| T2 | 实现 SsdTier、HddTier（ssd_tier.h/.cpp, hdd_tier.h/.cpp） | completed | 继承/封装 FileTier，交付物就绪 |
| T3 | 实现 TierManager（tier_manager.h/.cpp） | completed | 按优先级 Allocate，handle 映射，容量聚合 |
| T4 | 集成到 fluxcache_worker CMakeLists | completed | 编译通过 |
| T5 | 实现 ssd_tier_test、hdd_tier_test、tier_manager_test | completed | 覆盖验收用例，ctest 通过 |
| T6 | Review & Testing Gate | completed | 评审通过，ctest -R tier 通过 |

## 执行顺序

T1 → T2 → T3 → T4 → T5 → T6
