# Plan: P1-09 Memory Tier 基础能力

> Status: Done

## Goal

实现 Worker 端的最小存储层抽象与 Memory tier，支持页级数据块的分配、读写、释放和容量统计。

## 输入

- [design/storage-tier-memory-design.md](../design/storage-tier-memory-design.md)
- [issues/P1-09-storage-tier-memory.md](../issues/P1-09-storage-tier-memory.md)

## Steps

1. 扩展 `src/common/status.h/cpp`：新增 `StatusCode::kResourceExhausted` 与 `Status::ResourceExhausted()`。
2. 创建 `src/worker/storage/storage_tier.h`：定义 `TierBlockHandle`、`StorageTier` 接口。
3. 创建 `src/worker/storage/memory_tier.h`：定义 `MemoryTier` 类。
4. 创建 `src/worker/storage/memory_tier.cpp`：实现 `MemoryTier` 分配、读写、释放、容量统计。
5. 更新 `src/worker/CMakeLists.txt`：添加 storage 子目录或源文件。
6. 创建 `tests/worker/memory_tier_test.cpp`：覆盖 AllocateWriteRead、CapacityExhausted、ReleaseReclaimsCapacity、InvalidHandle、WriteReadOffset。
7. 更新 `tests/worker/CMakeLists.txt`：添加 memory_tier_test 并注册 ctest。

## 文件路径

| 文件 | 操作 |
|------|------|
| src/common/status.h | 修改 |
| src/common/status.cpp | 修改 |
| src/worker/storage/storage_tier.h | 新建 |
| src/worker/storage/memory_tier.h | 新建 |
| src/worker/storage/memory_tier.cpp | 新建 |
| src/worker/CMakeLists.txt | 修改 |
| tests/worker/memory_tier_test.cpp | 新建 |
| tests/worker/CMakeLists.txt | 修改 |

## 验证动作

- 构建：`cd build && cmake .. && cmake --build .`
- 测试：`cd build && ctest -R memory_tier -C Debug --output-on-failure`
- Lint：对改动文件执行 lint 检查

## 变更分级

P1 — 单服务核心逻辑、公共库接口（Status 扩展）、新增存储抽象。

## Risks & Assumptions

- **Risk**：Status 新增枚举需确保所有 switch 有 default 或显式处理。**Mitigation**：grep 检查 StatusCode 使用处。
- **Assumption**：Phase 1 不要求 MemoryTier 线程安全。
