# Todo: P2-05B Worker GC 与 orphan/misplaced block 对账

> Plan: [plan_p2-05b-worker-gc.md](./plan_p2-05b-worker-gc.md)

## Todo 列表

| # | 任务 | 状态 | DoD |
|---|------|------|-----|
| 1 | Proto 调整 Heartbeat 字段 | completed | Request: orphan_inode_ids, misplaced_block_ids; Response: audit_inode_ids, audit_block_ids |
| 2 | Master DeleteFile 实现 | completed | 删除 inode，记录 pending_orphan_inodes |
| 3 | MetaStore ScanPaginated | completed | 分页遍历接口，避免全量 ScanAll |
| 4 | Master Heartbeat 调度 | completed | 后台线程周期性调用 Worker.Heartbeat，下发 orphan/misplaced |
| 5 | Worker Heartbeat 处理与 GC | completed | 解析 Request，清理 orphan/misplaced，填充 Response audit |
| 6 | 集成测试 gc_reconciliation_test | completed | 可控 ring、假 MetaStore、DeleteFile→对账→验证 |
| 7 | 构建与 ctest 通过 | completed | cd build && cmake --build . && ctest --output-on-failure |

## 约束

- 全程最多一个 `in_progress`
- DoD 可验证
