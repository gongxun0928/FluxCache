# Master 元数据设计

> 日期: 2026-03-10（初版），2026-03-10（架构精化），2026-03-10（InodeTree 提前至 Phase 1）
> 状态: Phase 1 设计（含 InodeTree）+ Phase 2+ 优化愿景

## 1. 概述

本文档描述 FluxCache Master 的元数据管理设计。

- **Phase 1（当前实施）**：MountTable + **InodeTree（RocksDB inodes/edges）** + HashRingManager + WorkerManager，由 FileSystemMaster 无锁协调。元数据以 RocksDB 持久化，支持 UFS 已有文件的发现与 inode 关联。
- **Phase 2+（后续设计）**：Raft Journal、属性缓存、Rename 环路检测等进一步能力。

**核心原则**：

1. **组件锁独立**：每个子系统拥有独立的锁，锁永不嵌套
2. **协调层无锁**：FileSystemMaster 只编排调用顺序，不持有任何锁
3. **先完成一个组件的锁操作再进入下一个组件**：消除跨组件 TOCTOU 竞态
4. **Block 归属由 hash ring 计算决定**：不靠 Worker 上报登记，消除过期位置信息问题
5. **元数据持久化**：Phase 1 即使用 RocksDB 持久化 inodes/edges，支持 UFS list 同步建立 inode 关联

### 1.1 当前 source of truth

在当前 roadmap 中，Master 元数据的 source of truth 明确为：

- `InodeTree / InodeStore (RocksDB)`：命名空间、inode、目录关系
- `UFS mtime`：Phase 1 的缓存版本校验来源

当前**不**把 Journal 视为恢复主链路。HA / Journal 的边界见 [ha-journal-design.md](./ha-journal-design.md)。

---

## 2. Phase 1 架构

```
┌──────────────────────────────────────────────────────────────┐
│                    FileSystemMaster                           │
│                  (协调层，无自有锁)                             │
│                                                              │
│  ┌──────────────┐ ┌────────────────┐ ┌─────────────────────┐ │
│  │  MountTable   │ │  InodeTree     │ │   WorkerManager     │ │
│  │              │ │  (InodeStore)  │ │                     │ │
│  │   mu_        │ │                │ │      mu_            │ │
│  │ (shared_mu)  │ │  mu_ + RocksDB │ │    (mutex)          │ │
│  └──────────────┘ │  inodes/edges  │ └─────────────────────┘ │
│                   └────────────────┘                         │
│  ┌────────────────┐                                           │
│  │ HashRingManager│                                           │
│  │    mu_         │                                           │
│  └────────────────┘                                           │
└──────────────────────────────────────────────────────────────┘
```

InodeTree 内部持有 `next_id_`，负责 InodeId 分配与 path↔inode 映射。

### 2.1 InodeTree（InodeStore）

Phase 1 即实现基于 RocksDB 的 InodeTree，提供 path↔InodeId 映射与目录树结构。采用结构/属性分离设计，支持 UFS 已有文件的发现与 inode 关联。

#### 2.1.1 RocksDB Column Family

| CF | Key | Value |
|---|---|---|
| `inodes` | InodeId（8B 大端） | InodeEntry 紧凑二进制 |
| `edges` | ParentId(8B) + ChildName | ChildId(8B) |

- **inodes**：按 InodeId 查询文件/目录属性（size、mtime、block_size 等）
- **edges**：按父目录 + 子名构建目录树，支持 LookupPath、ListDirectory

#### 2.1.2 结构/属性分离

**内存结构（DirNode）** — 目录骨架常驻内存，路径解析零磁盘访问：

```cpp
struct DirNode {
    InodeId id;
    InodeId parent_id;
    std::string name;
    InodeState state;
    std::unordered_map<std::string, InodeId> children;
};
```

> **内存开销说明**：每个 DirNode 的 `children` unordered_map 每条目约 80-100 字节（bucket + hash + string heap + InodeId）。百万文件 / 十万目录规模下 children 总开销约 100MB+。Phase 1 接受此开销（换取路径解析零磁盘访问），Phase 2+ 可按需演进为惰性加载（冷目录的 children 从 edges CF 按需加载并用 LRU 淘汰），降低常驻内存。

**完整属性（Inode）** — 按需从 RocksDB inodes CF 读取：

```cpp
struct Inode {
    InodeId id;
    InodeId parent_id;
    std::string name;
    bool is_directory;
    uint64_t size;
    size_t block_size;
    int64_t creation_time_ms;
    int64_t modification_time_ms;
    uint64_t file_version;       // Phase 1 可用 mtime，Phase 2+ 自增
    // ...
};
```

#### 2.1.3 InodeEntry 紧凑序列化

```cpp
struct InodeEntry {
    uint64_t parent_id;
    uint64_t size;
    uint64_t block_size;
    int64_t creation_time_ms;
    int64_t modification_time_ms;
    uint64_t file_version;
    uint32_t mode;
    uint8_t flags;               // is_directory, is_complete, is_deleting
    uint8_t owner_id;            // dictionary-encoded
    uint8_t group_id;            // dictionary-encoded
    uint8_t _padding;
    // Variable: name (remaining bytes)
};
```

#### 2.1.4 InodeId 分配

InodeTree 内部维护 `next_id_`（原子计数器），CreateFile/CreateDirectory 时分配。Phase 1 持久化到 RocksDB 或单独元数据 key，重启恢复。

#### 2.1.5 两级分段锁

```cpp
mutable std::shared_mutex global_mu_;
static constexpr size_t kNumStripes = 256;
mutable std::array<std::shared_mutex, kNumStripes> stripe_locks_;
```

| 操作 | global_mu_ | stripe_locks_ |
|---|---|---|
| LookupPath | shared | shared（逐级） |
| CreateFile | shared | unique(parent) |
| Delete(file) | shared | unique(parent) |
| CreateDirectory | shared | unique(parent) |
| Delete(directory) | shared | unique(parent) |
| Rename | **unique** | — |

> **设计说明**：CreateDirectory 与 Delete(directory) 只需 `shared(global) + unique(parent_stripe)`，因为操作只修改 parent 的 children 映射，不影响其他目录树分支。仅 Rename（跨目录移动）需要 `unique(global)` 防止环路和并发结构变更。此设计避免了目录创建/删除全局串行化的瓶颈。

#### 2.1.6 UFS 已有文件与 inode 关联

UFS（如 S3、LocalFS）上已有文件是正常状态。元数据操作需通过 **UFS List** 发现文件列表，建立 inode 关联：

```
首次访问某挂载点目录时:
  ① MountTable.Resolve(path) → (ufs_uri, relative_path)
  ② Master 通过 UFS 接口 List(relative_path) 获取该目录下文件/子目录列表
  ③ 对每个 UFS 中的条目：
     - 若 InodeTree 中无对应 inode → 分配 InodeId，创建 inode 和 edge，写入 RocksDB
     - 若已有 inode → 可选：用 UFS GetStatus 刷新 size/mtime
  ④ 返回 List 结果（含 inode_id = InodeId）
```

**GetFileInfo(path)** 对已有文件：

- LookupPath 解析 path → 若 edge 存在则得到 InodeId
- 若 path 对应 UFS 中存在的文件但 InodeTree 无记录 → 触发**惰性同步**：List 父目录，为缺失条目创建 inode，再返回
- 从 inodes CF 读取 InodeEntry，结合 UFS GetStatus 获取最新 size/mtime（或信任缓存）

**同步策略**：Mount 时可选全量 List 预填充；或按需惰性同步（首次 GetFileInfo/List 时补齐）。

> **大目录保护**：对于 S3 等含大量对象的 UFS，同步 List 可能非常慢。Phase 1 采用分页 List + 后台异步同步：首次访问目录时仅同步请求所需的单个条目（GetFileInfo 场景）或第一批条目（ListDirectory 场景），完整目录同步放入后台任务队列，避免阻塞请求线程。

#### 2.1.7 恢复流程

1. 扫描 inodes CF，仅加载目录 → 构建 DirNode 骨架
2. 扫描 edges CF → 填充各目录 children，剪掉断引用
3. 孤儿目录检测 → 清理 parent 失效的目录
4. 恢复 `kDeleting` 状态的半删目录 → 重做清理
5. 恢复 `next_id_` 计数器
6. 首次启动时创建并持久化 root 目录

### 2.2 HashRingManager

Block 归属完全由一致性哈希计算决定。Master **不存储** Block→Worker 的映射关系，避免了上报登记模型中 Worker 上报延迟导致的过期位置信息问题。

```cpp
class HashRingManager {
    ConsistentHashRing ring_;              // 一致性哈希环（虚拟节点）
    uint64_t ring_version_{0};             // 递增版本号，拓扑变化时递增
    mutable std::shared_mutex mu_;
public:
    // Block 路由：根据 BlockId 计算目标 Worker
    WorkerId GetWorker(BlockId block_id) const;
    // 候选列表：返回有序候选 Worker（用于 fallback）
    std::vector<WorkerId> GetCandidates(BlockId block_id, int n) const;
    // 校验归属：判断某 Block 是否仍归指定 Worker 管
    bool IsOwner(BlockId block_id, WorkerId worker_id) const;
    // 拓扑变更
    void AddWorker(WorkerId, int virtual_nodes = 150);
    void RemoveWorker(WorkerId);
    // Client 同步
    uint64_t GetVersion() const;
    std::vector<WorkerEndpoint> GetRingSnapshot() const;
};
```

**路由逻辑**（处理 SUSPECT Worker）：

```
GetCandidates(block_id, n):
  candidates = hash_ring.GetNodes(block_id, n + suspect_count)
  返回前 n 个状态为 ALIVE 的 Worker（跳过 SUSPECT）
```

SUSPECT Worker 保留在 hash ring 中（不触发 rebalance），但路由时降低优先级。Worker 恢复后缓存数据依然有效。

**ring_version 的用途**：Client 本地缓存 hash ring 副本用于路由计算。ring_version 是缓存失效信号——告诉 Client 本地 ring 该刷新了。与数据正确性无关，只影响路由效率。

### 2.3 WorkerManager

管理 Worker 注册、心跳与健康状态，包含三态状态机。

```cpp
enum class WorkerState { ALIVE, SUSPECT, DEAD };

class WorkerManager {
    std::unordered_map<WorkerId, WorkerInfo> workers_;
    mutable std::mutex mu_;
public:
    void RegisterWorker(WorkerId, const WorkerInfo&);
    void HandleHeartbeat(WorkerId, const HeartbeatInfo&);
    void CheckWorkerHealth();  // 定期检查，驱动状态转换
    std::vector<WorkerInfo> GetAliveWorkers() const;
    WorkerState GetWorkerState(WorkerId) const;
};
```

**Worker 状态机**：

```
                 心跳恢复
            ┌───────────────┐
            ▼               │
  注册 → ALIVE ──心跳超时──→ SUSPECT ──宽限期到期──→ DEAD
                                                    │
                                              移出 hash ring
                                              ring_version++
```

| 状态 | 含义 | hash ring | 路由 |
|---|---|---|---|
| ALIVE | 正常服务 | 在环上 | 正常路由目标 |
| SUSPECT | 心跳超时，尚未确认下线 | **保留在环上** | 路由时跳过 |
| DEAD | 确认下线 | 移出环 | 不可路由 |

- 宽限期默认 5 分钟（配置项 `master.worker_suspect_timeout_ms`，默认 300000）
- SUSPECT 状态解决了绝大多数临时抖动场景（机器重启、网络闪断、滚动升级）
- 宽限期内恢复 → 回到 ALIVE，hash ring 不变，缓存数据全部保留
- 宽限期到期 → DEAD，移出 hash ring，ring_version 递增

### 2.5 FileSystemMaster 协调层

FileSystemMaster 不持有锁，通过**编排调用顺序**保证跨组件正确性。

#### DeleteFile：立即删除，立即回收

采用**立即删除**策略，不做延迟回收。删除 inode 后，Worker 缓存空间需尽快回收。

```
DeleteFile(path):
  ① mount_table_.Resolve(path)              ← MountTable 锁
  ② inode_tree_.LookupPath(path) → inode_id
  ③ inode_tree_.DeleteInode(inode_id)      ← 立即从 inodes/edges 删除，持久化到 RocksDB
  ④ 无需清理 Block 位置信息（hash ring 是纯计算，无状态）

Worker 端缓存回收:
  - 方案 A（推荐）：Heartbeat 对账。Worker 每次心跳上报 audit_inode_ids，Master 查 InodeTree 存在性，
    返回 orphan_inode_ids。Worker 收到后立即 DeleteBlockPages 清理。删除后最多延迟一个心跳周期（~10s）回收。
  - 方案 B：DeleteFile 时 Master 主动向相关 Worker 发送 InvalidateFile(inode_id) RPC，Worker 立即清理。
    实现复杂度更高，需 Master 知道哪些 Worker 缓存了该文件（hash ring 可推算）。
```

**不做 MarkDeleted**：删除即从 InodeTree 持久化移除，GC 对账时 `CheckFilesExist(audit_inode_ids)` 查 InodeTree 即可判断 inode_id 是否仍存在。

#### TruncateFile

```
TruncateFile(path, new_size):
  ① mount_table_.Resolve(path)
  ② inode_tree_.LookupPath(path) → inode_id
  ③ inode_tree_.UpdateSize(inode_id, new_size)  // 更新 inodes CF
  ④ 截断产生的过期 Block 由 Worker GC 对账清理（audit_block_ids 归属校验 + 文件 size 校验）
```

---

## 3. Block 回收与 GC

### 3.1 双重校验 GC

Worker 通过心跳中的滚动 GC 对本地缓存的 Block 进行双重校验：

```
Worker HeartbeatLoop (10s + jitter):
  常规心跳: 容量上报、健康状态

  滚动 GC 对账 (每次心跳附带一个桶):
    ① 取 audit_bucket 中的 block_ids (block_id % K == bucket)
    ② 按 inode_id 聚合去重

    校验 1 — 文件存在性:
      ③ 上报 audit_inode_ids → Master 查 InodeTree 判断 inode 是否仍存在
      ④ Master 返回 orphan_inode_ids（已删除的文件）
      ⑤ Worker 立即 DeleteBlockPages 清理

    校验 2 — Block 归属:
      ⑥ 上报 audit_block_ids → Master 用 hash ring 判断归属
      ⑦ Master 返回 misplaced_block_ids（不再归该 Worker 管的 Block）
      ⑧ Worker 清理 misplaced Block 的 Page

    ⑨ audit_bucket = (audit_bucket + 1) % K
```

- K=10 桶 × 10s/心跳 → 完整对账周期 ≈ 100~120s（含抖动）
- 校验 1 解决：文件被删除后 Worker 残留的孤儿 Block
- 校验 2 解决：拓扑变化后 Worker 空间充裕时幽灵缓存长期不淘汰的问题

### 3.2 自然淘汰补充（Phase 2+）

Phase 2+ 引入 LRU/LFU 淘汰策略后，GC 与淘汰互补：

- 空间紧张时：LRU/LFU 主动淘汰冷数据（含幽灵缓存）
- 空间充裕时：GC 校验 2 定期清理不再归属的 Block

> Phase 1 暂无淘汰策略，幽灵缓存依赖 GC 对账清理（Phase 2 实施）。

### 3.3 实施范围

Phase 1 在 Heartbeat proto 中**预留** `audit_inode_ids`、`audit_block_ids`、`orphan_inode_ids`、`misplaced_block_ids` 字段，但不实现完整 GC 逻辑。Phase 2 MetaStore（P2-05）落地后再实施完整的双重校验 GC：Master 用 InodeTree 查文件存在性 + hash ring 判断 Block 归属，Worker 从 MetaStore 遍历 block_ids 发起对账。

---

## 4. 文件版本与缓存一致性

### 4.1 问题场景

Worker 缓存了文件的 Block 数据后，UFS 上的文件可能被外部修改（或通过 write-through 更新），导致缓存数据过期。需要一种机制让 Worker 感知数据新鲜度。

### 4.2 Phase 1 方案：UFS mtime 作为简易版本

```
读路径:
  ① Client → Master.GetFileInfo(path)
  ② Master 从 InodeTree 查询 inode（若不存在则 UFS List 同步创建）
     MountTable.Resolve(path) → ufs_uri, ufs_path
     返回 inode_id(=InodeId), size, block_size, ufs_mtime_ms, ring_version, worker_list,
           ufs_uri, ufs_path
  ③ Client 本地计算 BlockId → hash ring 路由 → 目标 Worker
  ④ Client → Worker.ReadPages(block_id, page_indices, expected_mtime_ms, ufs_uri, ufs_path)
  ⑤ Worker 检查:
     cached_mtime == expected_mtime → 缓存命中，返回数据
     cached_mtime != expected_mtime → 淘汰旧 Page，用 ufs_uri+ufs_path 从 UFS 重新回源
```

```
写路径 (write-through):
  ① Client → Worker.WritePages(block_id, page_indices, data, ufs_uri, ufs_path)
  ② Worker 用 ufs_uri+ufs_path 写入 UFS → UFS mtime 自然更新
  ③ Worker 更新本地缓存 Page（记录新 mtime）
  ④ Client → Master.CompleteFile(id, size) → Master 更新 InodeTree 中 inode 的 size、mtime
```

### 4.3 Phase 2+ 演进：自增 file_version

Phase 1 的 InodeTree 已包含 `file_version` 预留字段（初始可等同于 mtime）。Phase 2+ 将 mtime 替换为 Master 维护的自增 `file_version`：

- 每次写操作递增 file_version，语义比 mtime 更精确（避免同秒内多次修改的冲突）
- file_version 存储在 Inode 属性中
- Worker 缓存的 Page 携带 `cached_version`，读取时比对
- 数据结构中预留 `uint64_t file_version` 字段

### 4.4 Worker 重启后的版本校验

Worker 重启后从 MetaStore（RocksDB）恢复 Page 缓存索引，每条 Page 携带 `cached_mtime_ms`。后续读请求自然通过 mtime 比对发现过期数据并回源，无需启动时全量校验。

---

## 5. Phase 2+ 优化方向（InodeTree 已在一期落地）

| 方向 | 说明 | 优先级 | 阶段 |
|---|---|---|---|
| Rename 环路检测 | 移动目录时上溯 parent 链检测环路，拒绝非法 Rename | 高 | Phase 2 |
| Raft Journal | InodeTree 写入经 Raft 复制，支持 Master HA | 高 | Phase 2 |
| file_version 自增 | mtime 替换为 Master 维护的自增 file_version，语义更精确 | 中 | Phase 2 |
| 属性缓存 | InodeId 的 LRU 缓存，减少 RocksDB Get | 中 | Phase 2 |
| HashRingManager 分段锁 | 读写分离 shared_mutex 已满足，高并发时可进一步优化 | 低 | Phase 3 |
| RocksDB 异步写入 | 内存先更新（锁内），RocksDB 批量异步写（锁外） | 低 | Phase 3 |
| Children 紧凑存储 | flat_hash_map 或小目录用 vector | 低 | Phase 3 |

---

## 6. 相关文件

| 文件 | 说明 |
|---|---|
| [block-id-and-file-layout.md](./block-id-and-file-layout.md) | 复合 BlockId 与文件布局设计 |
| `src/master/file_system_master.h/.cpp` | 协调层 |
| `src/master/inode_tree.h/.cpp` | 目录树与 path↔InodeId 映射（含 DirNode、InodeStore） |
| `src/master/inode_store.h/.cpp` | RocksDB inodes/edges 持久化 |
| `src/master/hash_ring_manager.h/.cpp` | 一致性哈希环管理（Block 路由） |
| `src/master/mount_table.h/.cpp` | 挂载表 |
| `src/master/worker_manager.h/.cpp` | Worker 管理（含状态机） |
