# HA Journal 设计

> 日期: 2026-03-10（初版），2026-03-11（Spike 完成）
> 状态: Spike 完成 — 设计收敛，待后续实现

## 1. 目标

在不打断当前 Phase 1 主线的前提下，提前收敛 Journal 与现有 RocksDB 元数据持久化之间的职责边界，为后续 HA / Raft 实现提供稳定设计基线。

本 Spike **不直接进入完整 HA 实现**，仅产出设计结论与实现拆分建议。

---

## 2. Spike 核心问题与结论

### 2.1 RocksDB 与 Journal 谁是重启恢复的 source of truth？

**结论：单一 source of truth，分阶段演进。**

| 阶段 | Source of Truth | 说明 |
|------|-----------------|------|
| **Phase 1 / 当前** | RocksDB（InodeStore inodes/edges） | 重启时仅从 RocksDB 恢复，Journal 不介入 |
| **HA 演进阶段** | RocksDB（状态机快照） | Journal 作为复制日志，RocksDB 为 apply 后的状态机快照 |

**原则**：任一时刻只存在**一个**恢复主链路，不允许「同时写 Journal 和 RocksDB，重启时再决定谁说了算」的竞争模式。

### 2.2 Journal 是复制通道、审计通道，还是恢复主链路？

**结论：Journal 在 HA 演进阶段承担「复制通道」职责，不承担恢复主链路。**

| 职责 | Phase 1 | HA 演进阶段 |
|------|---------|-------------|
| 复制通道 | 不适用 | ✓ 将元数据变更复制到 Follower |
| 审计通道 | 可选（非主路径） | 可选 |
| 恢复主链路 | ✗ | ✗ |

**恢复主链路**始终为 RocksDB（或基于 RocksDB 的快照 + Journal replay 补齐）。Journal 的 replay 仅用于：
- Follower 追上 Leader 的增量日志
- 或 Leader 从快照 + 增量 Journal 恢复（若采用 snapshot + log 模式）

**不允许**：Journal 与 RocksDB 同时作为「谁先恢复谁说了算」的竞争恢复源。

### 2.3 哪些 RPC / 元数据变更必须进入 Journal？

**必须写 Journal 的元数据变更**（改变 Master 持久化状态的操作）：

| RPC / 操作 | 变更对象 | 必须 Journal |
|------------|----------|--------------|
| `Mount` | MountTable | ✓ |
| `Unmount` | MountTable | ✓ |
| `CreateFile` | InodeTree (inodes + edges) | ✓ |
| `CompleteFile` | InodeTree (inode size/mtime) | ✓ |
| `DeleteFile` | InodeTree (inodes + edges) | ✓ |
| `CreateDirectory` | InodeTree | ✓（若实现） |
| `DeleteDirectory` | InodeTree | ✓（若实现） |
| `RegisterWorker` | WorkerManager + HashRingManager | 可选（见下） |

**不写 Journal 的操作**：
- `GetFileInfo`、`ListMounts`、`GetHashRing`：只读，无状态变更
- `SyncFromUfs`（惰性同步）：UFS 发现并创建 inode，**必须**写 Journal（等价于 CreateFile/CreateDirectory 的批量形式）

**RegisterWorker 的边界**：若将 Worker 拓扑视为可恢复元数据（重启后需恢复 hash ring），则需 Journal；若采用「重启后 Worker 重新注册」策略，则可不 Journal。建议 HA 阶段再定。

### 2.4 Checkpoint 如何与 inodes/edges 协同？

**结论：Checkpoint 是 RocksDB 状态快照，Journal 是增量日志。**

```
┌─────────────────────────────────────────────────────────────────┐
│                     HA 演进阶段数据流                              │
├─────────────────────────────────────────────────────────────────┤
│  Client RPC                                                      │
│       │                                                          │
│       ▼                                                          │
│  Journal append（复制通道，Leader 写 → Follower 复制）              │
│       │                                                          │
│       ▼                                                          │
│  Apply to state machine（InodeTree + MountTable 等）               │
│       │                                                          │
│       ▼                                                          │
│  RocksDB 持久化（inodes/edges）                                    │
│       │                                                          │
│       ▼                                                          │
│  Checkpoint（周期性快照 RocksDB → 独立文件/目录）                   │
│       │                                                          │
│       └── Checkpoint 之后可 truncate Journal 前缀                 │
└─────────────────────────────────────────────────────────────────┘
```

**协同规则**：
1. Checkpoint 时：将当前 RocksDB inodes/edges 导出为快照，记录 `checkpoint_index`（对应 Journal 的 apply 进度）
2. 恢复时：加载最新 Checkpoint → replay 该 Checkpoint 之后的 Journal 条目
3. Journal 前缀 truncate：仅当该前缀对应的所有条目已包含在 Checkpoint 中，且 Follower 已确认同步

---

## 3. RocksDB 与 Journal 职责边界

| 组件 | 职责 | 持久化内容 |
|------|------|------------|
| **RocksDB** | 状态机存储、重启恢复主链路 | inodes CF、edges CF、next_id、MountTable（若持久化） |
| **Journal** | 复制通道、增量日志 | 有序 JournalEntry 序列 |
| **Checkpoint** | 状态快照、缩短 Journal 长度 | RocksDB 快照或导出文件 |

**边界原则**：
- RocksDB：存储「当前状态」的权威副本
- Journal：存储「如何从上一状态到达当前状态」的变更序列
- 恢复时：**仅**从 RocksDB（或 Checkpoint + Journal replay）恢复，无第二条竞争路径

---

## 4. 恢复流程（单一主链路）

### 4.1 Phase 1 当前恢复流程

```
Master 启动
    │
    ▼
加载 RocksDB inodes/edges
    │
    ▼
构建 DirNode 骨架、恢复 next_id_
    │
    ▼
MountTable 从配置或空（当前实现为内存）
    │
    ▼
就绪
```

### 4.2 HA 演进阶段恢复流程（设计目标）

```
Master 启动（Standalone 或 Raft Follower）
    │
    ▼
确定恢复源：最新 Checkpoint（若存在）
    │
    ▼
加载 Checkpoint → 恢复 RocksDB 状态
    │
    ▼
Replay Checkpoint 之后的 Journal 条目（若有）
    │
    ▼
就绪
```

**禁止**：同时存在「从 Journal 全量 replay」与「从 RocksDB 直接加载」两条互相竞争的恢复路径。恢复逻辑必须**二选一**，由「是否存在有效 Checkpoint」等条件明确分支。

---

## 5. JournalEntry 最小模型草案

### 5.1 类型定义（接口草图）

```cpp
// 草案：仅用于设计讨论，不改变现有主路径
enum class JournalOp : uint8_t {
  kMount = 1,
  kUnmount = 2,
  kCreateFile = 3,
  kCompleteFile = 4,
  kDeleteFile = 5,
  kCreateDirectory = 6,
  kDeleteDirectory = 7,
  kSyncFromUfsCreate = 8,  // 惰性同步创建的 inode/edge
  // kRegisterWorker = 9,  // 可选
};

struct JournalEntry {
  uint64_t index;           // 单调递增，全局唯一
  int64_t timestamp_ms;     // 用于审计/调试
  JournalOp op;
  std::vector<uint8_t> payload;  // 操作相关序列化数据
};

// 序列化：index(8) + timestamp(8) + op(1) + len(4) + payload
// 或使用 protobuf / 自定义二进制格式
```

### 5.2 各操作的 Payload 草案

| Op | Payload 内容（示意） |
|----|----------------------|
| kMount | path, ufs_uri |
| kUnmount | path |
| kCreateFile | path, inode_id, parent_id, block_size, creation_time_ms |
| kCompleteFile | inode_id, size, mtime_ms |
| kDeleteFile | path, inode_id |
| kCreateDirectory | path, inode_id, parent_id |
| kDeleteDirectory | path, inode_id |
| kSyncFromUfsCreate | path, inode_id, parent_id, is_directory, size, mtime_ms |

### 5.3 实现范围（Spike 不承诺）

- 本 Spike 仅产出模型草案与序列化格式建议
- 实际实现时需与 InodeEntry、MountTable 等现有结构对齐
- 若需要代码，仅限 `src/master/ha/journal_entry.h` 等接口草图，**不**修改 `master_service_impl.cpp` 等主路径

---

## 6. 关键写路径时序图

### 6.1 当前 Phase 1 写路径（CreateFile）

```
Client          MasterServiceImpl      MountTable    PathResolver   InodeTree      InodeStore(RocksDB)
  │                     │                    │              │              │                    │
  │ CreateFile(path)    │                    │              │              │                    │
  │───────────────────>│                    │              │              │                    │
  │                    │ Resolve(path)      │              │              │                    │
  │                    │───────────────────>│              │              │                    │
  │                    │<───────────────────│ ufs_uri,     │              │                    │
  │                    │                    │ ufs_path     │              │                    │
  │                    │ SyncFromUfs(dir)   │              │              │                    │
  │                    │──────────────────────────────────>│              │                    │
  │                    │<──────────────────────────────────│              │                    │
  │                    │ LookupPath(path)   │              │              │                    │
  │                    │─────────────────────────────────────────────────>│                    │
  │                    │<─────────────────────────────────────────────────│                    │
  │                    │ CreateFile(...)    │              │              │                    │
  │                    │─────────────────────────────────────────────────>│                    │
  │                    │                    │              │   Put inodes │                    │
  │                    │                    │              │   Put edges  │                    │
  │                    │                    │              │─────────────┼───────────────────>│
  │                    │<─────────────────────────────────────────────────│                    │
  │<───────────────────│                    │              │              │                    │
```

### 6.2 HA 演进阶段写路径（CreateFile，设计目标）

```
Client    MasterServiceImpl   JournalWriter   Raft/Replication   InodeTree   RocksDB
  │               │                  │                │              │         │
  │ CreateFile    │                  │                │              │         │
  │──────────────>│                  │                │              │         │
  │               │ Append(entry)     │                │              │         │
  │               │─────────────────>│                │              │         │
  │               │                  │ Replicate      │              │         │
  │               │                  │───────────────>│              │         │
  │               │                  │<───────────────│ (quorum ack)  │         │
  │               │                  │                │              │         │
  │               │ Apply(entry)     │                │              │         │
  │               │─────────────────────────────────────────────────>│         │
  │               │                    │                │   Put      │         │
  │               │                    │                │────────────┼────────>│
  │               │<─────────────────────────────────────────────────│         │
  │<──────────────│                  │                │              │         │
```

**顺序约束**：先 Journal append + 复制确认，再 Apply 到状态机。避免「先写 RocksDB 再 Journal」导致 Follower 与 Leader 状态分叉。

### 6.3 CompleteFile 写路径（当前）

```
Client          MasterServiceImpl      InodeTree      InodeStore(RocksDB)
  │                     │                    │                    │
  │ CompleteFile(id,sz)  │                    │                    │
  │─────────────────────>│                    │                    │
  │                     │ GetInode(id)       │                    │
  │                     │───────────────────>│                    │
  │                     │ UpdateSizeAndMtime  │                    │
  │                     │───────────────────>│   Put inodes       │
  │                     │                    │───────────────────>│
  │                     │<───────────────────│                    │
  │<─────────────────────│                    │                    │
```

---

## 7. 风险清单

| 风险 | 等级 | 描述 | 缓解措施 |
|------|------|------|----------|
| 双写竞争 | 高 | 若 Journal 与 RocksDB 并行写且顺序不一致，恢复时状态分叉 | 严格「先 Journal 后 Apply」顺序；Phase 1 不引入 Journal 写 |
| 恢复路径竞争 | 高 | 同时存在两条恢复主链路，导致行为不确定 | 设计上禁止；恢复逻辑单一分支，由 Checkpoint 存在性决定 |
| Journal 无限增长 | 中 | 不 truncate 则磁盘占满 | 引入 Checkpoint + truncate 机制；Spike 阶段仅设计，不实现 |
| MountTable 未持久化 | 中 | 当前 MountTable 仅内存，重启丢失 | HA 阶段需持久化 MountTable，或从 Journal replay 恢复 |
| SyncFromUfs 批量写 | 中 | 惰性同步可能一次创建大量 inode，Journal 条目爆炸 | 考虑批量 JournalEntry 或压缩；后续实现时细化 |
| RegisterWorker 边界 | 低 | 是否 Journal 影响恢复语义 | HA 阶段明确：重启后 Worker 重注册 vs 持久化拓扑 |

---

## 8. 后续实现拆分建议

按依赖顺序建议的实现步骤：

| 步骤 | 内容 | 产出 |
|------|------|------|
| 1 | JournalEntry 模型与序列化 | `journal_entry.h/.cpp`，单元测试 |
| 2 | Journal writer / reader 原型 | 本地 append-only 日志，无复制 |
| 3 | 元数据写路径的 journal hook | CreateFile/CompleteFile 等调用 JournalWriter（可配置关闭） |
| 4 | Checkpoint 与快照恢复 | RocksDB 快照导出，恢复时加载 |
| 5 | Raft 复制与 leader 选举 | 依赖步骤 2–4，引入 Raft 库 |

**Spike 约束**：步骤 1–2 可产出最小原型；步骤 3 仅接口草图或 feature-flag 关闭的占位，**不改变现有主路径行为**。

---

## 9. 相关文件

| 文件 | 说明 |
|------|------|
| [metadata-design.md](./metadata-design.md) | Master 元数据架构，InodeTree/RocksDB 恢复流程 |
| [block-id-and-file-layout.md](./block-id-and-file-layout.md) | BlockId 编码与文件布局 |
| [P2-10-master-ha-raft.md](../issues/P2-10-master-ha-raft.md) | 本 Spike 的 issue 定义 |
