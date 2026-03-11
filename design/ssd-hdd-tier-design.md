# SSD/HDD Tier 设计

> 关联 Issue: P2-01
> 依赖: P1-09 Memory Tier

## 1. 概述

在 Memory tier 之外补齐基于本地文件的 SSD/HDD tier，实现 `StorageTier` 接口，为恢复和多层缓存演进打基础。

## 2. 接口与约束

- 复用 `StorageTier` 接口（`storage_tier.h`）
- `TierBlockHandle` 语义不变：`id` 为块唯一标识
- SSD/HDD 与 Memory 行为一致：分配、读写、释放、容量统计

## 3. FileTier 通用设计（SSD/HDD 共用）

### 3.1 存储布局

- **根目录**：由构造参数传入（如 `/data/ssd`、`/data/hdd`）
- **块文件**：`block-{id}.dat`，每个块一个文件
- **元数据**：块大小由文件 size 推断，无需额外元数据文件

### 3.2 持久化与恢复

- 启动时若目录已存在：扫描 `block-*.dat`，累加 `used_`，`next_id_ = max(id)+1`
- 启动时若目录不存在：`create_directories`，`used_=0`，`next_id_=1`
- 验收「重启后磁盘上的 tier 文件仍存在」：写入后析构再重建，Read 可读到原数据

### 3.3 实现要点

- 使用 `std::filesystem`、`std::fstream` 做文件 I/O
- 线程安全：当前 P2-01 不要求并发，与 MemoryTier 一致（单线程调用假设）
- 错误：IO 失败返回 `Status::IOError`

## 4. TierManager 设计

### 4.1 职责

- 持有多个 `StorageTier*`（或 `unique_ptr`），按优先级排序
- `Allocate(size, handle)`：按优先级依次尝试各 tier，直到成功或全部失败
- `Write/Read/Release`：直接转发到持有该 handle 的 tier（需记录 handle -> tier 映射）

### 4.2 Handle 与 Tier 映射

- `TierBlockHandle` 仅含 `id`，无法直接判断归属 tier
- 方案：每个 tier 的 id 空间独立（Memory/SSD/HDD 各自 `next_id_`），则 id 可能冲突
- 更稳妥：TierManager 维护 `handle.id -> tier*` 映射，或扩展 Handle 携带 tier 索引
- **决策**：TierManager 内部用 `std::unordered_map<uint64_t, StorageTier*> handle_to_tier_`，Allocate 成功时插入，Release 时删除

### 4.3 优先级

- 构造时传入 tier 列表，顺序即优先级（先传入的优先）
- 例如：`[Memory, SSD, HDD]` → 优先用 Memory，满则 SSD，再 HDD

### 4.4 容量统计

- `UsedCapacity()`：各 tier 的 `UsedCapacity()` 之和
- `CapacityLimit()`：各 tier 的 `CapacityLimit()` 之和
- 可选：提供 `TierStats` 返回每个 tier 的 used/limit

## 5. 测试设计

| 用例 | 目标 |
|------|------|
| SSD/HDD AllocateWriteRead | 分配、写、读、释放，行为与 MemoryTier 一致 |
| SSD/HDD CapacityExhausted | 容量耗尽时返回 ResourceExhausted |
| SSD/HDD PersistenceAcrossRestart | 写入后析构，用同目录重建，Read 可读到原数据 |
| TierManager PrioritySelection | 按优先级选择 tier，高优满时用次优 |
| TierManager CapacityStats | 各 tier 容量统计正确 |
| TierManager InvalidHandle | 无效 handle 返回 InvalidArgument |

## 6. 风险与假设

- **风险**：目录被外部删除或权限变化导致 IO 失败。**Mitigation**：返回明确 IOError，调用方处理。
- **假设**：单进程单线程使用，与 MemoryTier 一致；后续 P2-02 再考虑并发与晋升流程。
