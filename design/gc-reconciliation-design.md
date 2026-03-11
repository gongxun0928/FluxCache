# P2-05B: Worker GC 与 orphan/misplaced block 对账设计

> 关联 Issue: P2-05b-worker-gc-reconciliation
> 依赖: P2-05 (MetaStore), P1-05C (HashRing), P1-15D (Write path)

## 1. 概述

在 MetaStore 可恢复页索引之后，实现 Worker 的最小对账 GC：清理已删除文件的 orphan block，以及拓扑变化后已不再归属本 Worker 的 misplaced block。对账过程不阻塞正常读写主路径。

## 2. 对账流程（参考 metadata-design.md §3.1）

```
Master 周期性调用 Worker.Heartbeat:
  Request (Master → Worker): worker_id, orphan_inode_ids, misplaced_block_ids
  Response (Worker → Master): audit_inode_ids, audit_block_ids

Worker 收到后:
  ① 对 orphan_inode_ids: 遍历 MetaStore 分桶/分页，清理 GetInodeId(block_id) ∈ orphan 的 block
  ② 对 misplaced_block_ids: 直接 DeleteBlockPages(block_id)
  ③ 本地计算 misplaced: 分桶遍历 MetaStore，若 GetWorker(block_id) != my_id 则清理
  ④ 下次 Heartbeat 上报 audit_inode_ids, audit_block_ids（已清理的）
```

## 3. Proto 字段语义

当前 worker.proto 定义：
- HeartbeatRequest: worker_id, audit_inode_ids, audit_block_ids
- HeartbeatResponse: orphan_inode_ids, misplaced_block_ids

**语义**：Master 为 client，Worker 为 server。Master 调用 Worker.Heartbeat。
- **Request**：Master 发送 worker_id（标识目标 Worker）、orphan_inode_ids（已删除文件）、misplaced_block_ids（不再归属的 block）
- **Response**：Worker 返回 audit_inode_ids、audit_block_ids（本轮已清理的，供 Master 确认）

需调整 proto：将 orphan/misplaced 放入 Request，audit 放入 Response。

## 4. Master 侧逻辑

### 4.1 数据来源

- **orphan_inode_ids**：DeleteFile 时记录待回收 inode_id，Heartbeat 时下发；或 Master 维护 `pending_orphan_inodes_` 队列，DeleteFile 入队，Heartbeat 出队下发。
- **misplaced_block_ids**：Worker 上报其 block 列表，Master 用 HashRing 计算归属，返回不属于该 Worker 的。为减少传输，采用 **Worker 本地计算**：Master 在 Response 中返回 ring_version + worker_list（或 Worker 定期 GetHashRing），Worker 自行判断 misplaced。

**简化**：Phase 1 仅实现 orphan 由 Master 下发；misplaced 由 Worker 本地计算（Worker 需能调用 HashRing 归属判断，可通过 Master 提供 GetHashRing 或 Heartbeat Response 携带 ring 快照）。

### 4.2 调用方

Master 需周期性向各 Worker 发起 Heartbeat。当前 Worker 通过 RegisterWorker 向 Master 心跳。两种方案：
- **A**：Master 后台线程周期性调用各 Worker 的 WorkerService.Heartbeat
- **B**：在 RegisterWorker 响应中扩展，携带 orphan/misplaced

采用 **方案 A**：Master 维护 Heartbeat 调度，主动调用 Worker.Heartbeat，语义清晰。

## 5. Worker 侧逻辑

### 5.1 接收与清理

- 收到 orphan_inode_ids：分桶/分页遍历 MetaStore，对每个 block_id 若 `GetInodeId(block_id) ∈ orphan_inode_ids`，则 DeleteBlockPages(block_id)。
- 收到 misplaced_block_ids：直接 DeleteBlockPages(block_id)。
- 本地 misplaced：分桶遍历 MetaStore，对每个 block_id 若 `HashRing.GetWorker(block_id) != my_worker_id`，则 DeleteBlockPages(block_id)。Worker 需持有 ring 快照，可从 GetHashRing 或 Heartbeat Response 获取。

### 5.2 分桶/分页遍历

避免 ScanAll 一次全量扫描：
- **分桶**：`ScanByBucket(bucket_index, num_buckets, callback)`，仅处理 `block_id % num_buckets == bucket_index` 的条目。
- **分页**：`ScanPaginated(start_after, limit, callback)`，每次处理 limit 条，返回 next_start。

采用 **分页** 更易实现：MetaStore 增加 `ScanPaginated(std::optional<PageId> start, size_t limit, callback)`，每次 Heartbeat 处理一页，多轮完成全量。

### 5.3 非阻塞

GC 在后台线程或 Heartbeat 处理线程执行，不持有 PageStore 的长时间锁。DeleteBlockPages 为短时操作，可接受。

## 6. DeleteFile 实现

P2-05B 需 Master 实现 DeleteFile（当前 UNIMPLEMENTED），以便产生 orphan inode。DeleteFile 流程：
1. Resolve path → inode_id
2. InodeTree.DeleteInode(inode_id)
3. 将 inode_id 加入 pending_orphan_inodes_，供 Heartbeat 下发

## 7. 测试设计

| 用例 | 目标 |
|------|------|
| OrphanCleanup | 写文件 → DeleteFile → Heartbeat 对账 → 验证缓存页已清理 |
| MisplacedCleanup | 单 Worker 写 block → 模拟 ring 变化（或双 Worker 场景）→ 对账清理不再归属的 block |
| NonBlocking | 对账过程中并发 Read/Write 不受阻塞 |
| PaginatedScan | 使用假 MetaStore 数据，验证分页遍历正确性 |

测试使用 FakeUfs、可控 HashRing（测试用 HashRingManager 或 mock）、假 MetaStore 数据，不依赖长时间后台线程偶发触发。

## 8. 风险与假设

- **风险**：Master 调用 Worker.Heartbeat 需 Master 持有 Worker 的 gRPC 地址，当前 WorkerManager 已有 host/port。
- **假设**：单 Worker 场景下 misplaced 为空（ring 仅一个节点）；多 Worker 时需真实 ring 变化验证。
