# Block ID 与文件布局设计

> 日期: 2026-03-10
> 状态: Phase 1 设计

## 1. 概述

FluxCache 采用 **Block/Page 两层模型**：

- **Block**（默认 64MB）：文件的逻辑分割与分布式放置单元。Master 管理 Block→Worker 映射，Client 按 BlockId 路由到目标 Worker。
- **Page**（默认 1MB）：Worker 内部的缓存调度单元。一个 Block 包含最多 64 个 Page，按需缓存到 Memory/SSD/HDD 多级存储。

```
文件 (任意大小)
  └── Block 0 (64MB)        ← 分布到 Worker A
  │     ├── Page 0 (1MB)    ← 缓存在 Memory
  │     ├── Page 1 (1MB)    ← 缓存在 SSD
  │     └── ...
  └── Block 1 (64MB)        ← 分布到 Worker B
  │     ├── Page 0 (1MB)
  │     └── ...
  └── Block 2 (不足 64MB)   ← 分布到 Worker C
        └── Page 0 ~ N
```

**核心设计思想**：

1. 复合 BlockId 使文件→块映射**零存储**：所有 BlockId 由 `(inode_id, file_size, block_size)` 推导。
2. Block 层提供**分布式条带化**：同文件不同 Block 哈希值不同，自动分散到不同 Worker。
3. Page 层提供**细粒度缓存**：只缓存实际访问到的 Page，避免整块 Block 浪费缓存空间。

---

## 2. 复合 BlockId 编码

### 2.1 位布局

将 `(InodeId, BlockIndex)` 编码到一个 `uint64_t`（定义在 `src/common/types.h`）：

```
BlockId (uint64_t, 64 bits)
┌─────────────────────────────┬──────────────────────┐
│      InodeId (40 bits)      │  BlockIndex (24 bits) │
│       bits [63:24]          │  bits [23:0]          │
└─────────────────────────────┴──────────────────────┘
```

| 字段 | 位数 | 范围 | 含义 |
|---|---|---|---|
| InodeId | 40 | 0 ~ 1,099,511,627,775 | 约 **1.1 万亿**个唯一文件 |
| BlockIndex | 24 | 0 ~ 16,777,215 | 每文件最多 **1677 万**个 Block |

最大文件大小（默认 64MB block_size）：`16,777,216 × 64 MB = 1 PB`。

InodeId 耗尽时间（每秒创建 100 万个文件）：`1.1 × 10^12 / 10^6 / 86400 / 365 ≈ 34.8 年`。

### 2.2 辅助函数

```cpp
using InodeId = uint64_t;
using BlockId = uint64_t;

constexpr int kInodeIdBits = 40;
constexpr int kBlockIndexBits = 24;
constexpr BlockId kInvalidBlockId = 0;
constexpr InodeId kInvalidInodeId = 0;

inline BlockId MakeBlockId(InodeId inode_id, uint32_t block_index);
inline InodeId GetInodeId(BlockId block_id);
inline uint32_t GetBlockIndex(BlockId block_id);
inline uint32_t GetBlockCount(uint64_t file_size, size_t block_size);
inline size_t   GetBlockLength(uint64_t file_size, uint32_t block_index, size_t block_size);
```

### 2.3 InodeId 分配

Phase 1 阶段，Master 使用轻量原子计数器分配 InodeId：

```cpp
std::atomic<InodeId> next_id_{1};      // 0 保留为 invalid
std::atomic<InodeId> alloc_end_{1};    // 预分配上界

InodeId AllocateInodeId() {
    InodeId id = next_id_.fetch_add(1);
    if (id >= alloc_end_.load()) {
        // CAS 竞争扩展预分配范围（默认 1000/批）
        // 持久化新的 alloc_end 到本地文件
    }
    return id;
}
```

InodeId 分配逻辑统一由 InodeTree 负责。

---

## 3. 文件→Block 隐式映射

文件**不存储 Block 列表**。所有 BlockId 由三个值推导：

```
block_count = ceil(file_size / block_size)
block_ids[i] = MakeBlockId(inode_id, i)   for i in 0..block_count-1
```

**示例**（`inode_id=42, size=200MB, block_size=64MB`）：

```
block_count = ceil(200MB / 64MB) = 4
block_ids  = [MakeBlockId(42, 0),  MakeBlockId(42, 1),
              MakeBlockId(42, 2),  MakeBlockId(42, 3)]
```

| 操作 | 实现 |
|---|---|
| 写入第 N 个 Block | `MakeBlockId(inode_id, N)` → Client 本地计算，直接路由到 Worker |
| 查询文件所有 Block | `for i in 0..block_count: MakeBlockId(inode_id, i)` |
| 给定 BlockId 查所属文件 | `GetInodeId(block_id)` → 位运算 |
| 给定 BlockId 查文件内偏移 | `GetBlockIndex(block_id) * block_size` |

隐式映射在 10 亿文件 × 平均 10 Block 的场景下，节省约 **80 GB** 内存（无需存储 100 亿个 BlockId 引用）。

---

## 4. Block→Page 关系

### 4.1 PageId 定义

Phase 1 采用结构体方案，清晰表达 Block 内 Page 的从属关系：

```cpp
struct PageId {
    BlockId  block_id;      // 所属 Block
    uint16_t page_index;    // Block 内的 Page 序号

    bool operator==(const PageId& o) const;
    // 用于 unordered_map 的 hash 支持
};
```

每个 Block（64MB）最多包含 `64MB / 1MB = 64` 个 Page，`uint16_t` 足够覆盖（最大 65535）。

### 4.2 Page 推导

与 Block 隐式映射类似，Block 内的 Page 列表也可推导：

```
pages_per_block = block_size / page_size          // 默认 64
actual_pages    = ceil(block_length / page_size)   // 最后一个 Block 可能不满
page_ids[j]     = {block_id, j}                    for j in 0..actual_pages-1
```

### 4.3 未来优化方向

如果 Worker 内存索引成为瓶颈，可将 PageId 紧凑编码到单个 `uint64_t`：

```
PageId (uint64_t, 64 bits)
  BlockId    (48 bits) [63:16]  — InodeId 34bit + BlockIndex 14bit
  PageIndex  (16 bits) [15:0]   — 每 Block 最多 65536 个 Page
```

Phase 1 不实施此优化，仅记录方案备用。

---

## 5. 一致性哈希与 Block 放置

### 5.1 纯 hash ring 计算模型

Block 归属完全由一致性哈希计算决定，**Master 不存储** Block→Worker 的映射关系（无 `block_locations_` 登记表）。

这种"纯计算"模型相比"Worker 上报 → Master 登记"模型的优势：

| 维度 | 上报登记模型 | 纯 hash ring 计算 |
|---|---|---|
| 一致性 | 可能过期（Worker 已淘汰但未上报） | **始终一致**（计算结果 = 当前拓扑） |
| Master 内存 | 存所有 Block 位置 | **只存 Worker 列表 + hash ring** |
| Worker 状态 | 需维护 InodeId→path 映射 | **无状态**（UFS 路径由 Client 透传） |
| 心跳负载 | 上报 block 列表 | **只上报容量和健康状态** |
| Client 路由 | 每次问 Master | **本地缓存 ring，本地计算** |

### 5.2 Block 条带化

复合 BlockId 天然适配一致性哈希：

- 同一文件的不同 Block，因 BlockIndex 不同导致 BlockId 哈希值不同，自动分散到不同 Worker
- 实现数据条带化，提升并行读写吞吐
- 每个 Worker 映射 150 个虚拟节点，Worker 增减时只影响约 1/N 的 Block

### 5.3 Client 本地路由

Client 缓存 hash ring 副本（带 `ring_version`），后续读写请求本地计算目标 Worker，无需每次向 Master 查询：

```
Client 读文件流程:
  1. GetFileInfo → 获得 inode_id, file_size, block_size, ufs_mtime, ring_version, worker_list,
                   ufs_uri, ufs_path
  2. 更新本地 ring 缓存（如 ring_version 变化）
  3. 本地计算 block_id = MakeBlockId(inode_id, offset / block_size)
  4. 本地 hash ring 计算 → 目标 Worker（跳过 SUSPECT）
  5. 向 Worker 发 ReadPages(block_id, page_indices, expected_mtime, ufs_uri, ufs_path)
     Worker cache miss 时使用 ufs_uri+ufs_path 直接回源，无需向 Master 查询路径
```

**Phase 1 简化**：单 Worker 场景，hash ring 只有一个节点。接口已预留多 Worker 扩展。

---

## 6. 边界场景

### 6.1 文件追加写（Append）

```
原文件: size=128MB → block_count=2 (index 0, 1)
追加 50MB: size=178MB → block_count=3 (index 0, 1, 2)
新 block_id = MakeBlockId(inode_id, 2) // 确定性，无需协调
```

Block 0、1 的 BlockId 不变，已有缓存仍然有效。只需写入新的 Block 2。

### 6.2 文件截断（Truncate）

```
原文件: size=200MB → blocks 0,1,2,3
Truncate to 100MB → blocks 0,1 保留, blocks 2,3 作废
→ 通知 Worker 异步回收 blocks 2,3 的所有 Page
```

### 6.3 InodeId 不复用

删除文件后 InodeId 不回收，避免新旧文件 BlockId 冲突。40 位空间在每秒百万次创建下可用约 34 年。

### 6.4 Block 大小配置

`block_size` 通过配置项 `master.block_size_mb`（默认 64）控制，全局统一。同一集群内所有文件使用相同 block_size，简化 Block 放置与推导逻辑。

---

## 7. 相关文件

| 文件 | 说明 |
|---|---|
| `src/common/types.h` | InodeId、BlockId、PageId 类型与辅助函数 |
| `src/master/hash_ring_manager.h/.cpp` | 一致性哈希环管理（Block 路由计算） |
| `src/master/inode_tree.h/.cpp` | InodeId 分配与目录树管理 |
| `src/worker/page/page_store.h/.cpp` | Worker 内 Page 级缓存引擎 |
| `proto/common.proto` | WorkerEndpoint、FileInfo 等消息定义 |

---

## 8. 与 AnyCache 的差异

| 维度 | AnyCache | FluxCache |
|---|---|---|
| 缓存粒度 | Block（64MB） | Block（64MB） + Page（1MB）两层 |
| ID 编码 | BlockId = (InodeId, BlockIndex) | BlockId = (InodeId, BlockIndex)，PageId = 结构体 |
| 文件标识 | InodeId（InodeTree 分配） | InodeId（Phase 1 即 InodeTree 分配） |
| 缓存利用率 | 整块缓存 | 按 Page 按需缓存，利用率更高 |
| Block 路由 | BlockMaster（上报登记） | HashRingManager（纯 hash ring 计算） |
| 缓存校验 | 无 | UFS mtime 校验（Phase 1），file_version（Phase 2+） |
| Master 元数据 | InodeTree + BlockMaster | MountTable + InodeTree + HashRingManager（Phase 1） |
