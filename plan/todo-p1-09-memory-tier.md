# Todo: P1-09 Memory Tier 基础能力

> 关联: [plan_p1-09-memory-tier.md](plan_p1-09-memory-tier.md)

## Todo 列表

| ID | 内容 | 状态 | DoD |
|----|------|------|-----|
| T1 | 扩展 Status：kResourceExhausted | done | status.h/cpp 新增，编译通过 |
| T2 | 创建 storage_tier.h：TierBlockHandle、StorageTier | done | 接口定义完整，可被 MemoryTier 继承 |
| T3 | 创建 memory_tier.h/.cpp：MemoryTier 实现 | done | 分配、读写、释放、容量统计实现完成 |
| T4 | 更新 worker CMakeLists：添加 storage 源 | done | fluxcache_worker 链接 storage 并编译通过 |
| T5 | 创建 memory_tier_test.cpp | done | 6 个用例全部通过 |
| T6 | 更新 tests/worker CMakeLists | done | memory_tier_test 注册 ctest |

## 执行顺序

T1 → T2 → T3 → T4 → T5 → T6

## 验收

- `ctest -R memory_tier` 全部通过
- 无新增 lint 问题
