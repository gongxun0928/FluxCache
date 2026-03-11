# Plan: P2-05B Worker GC 与 orphan/misplaced block 对账

> Status: Done
> 变更分级: P1
> 设计文档: design/gc-reconciliation-design.md

## Goal

实现 Heartbeat 对账字段的真实使用，完成 orphan inode 与 misplaced block 的清理，分桶/分页遍历 MetaStore 避免全量扫描，对账不阻塞读写主路径。

## Steps

1. **Proto 调整**：HeartbeatRequest 增加 orphan_inode_ids、misplaced_block_ids；HeartbeatResponse 增加 audit_inode_ids、audit_block_ids（与现有字段语义对齐，必要时重命名）
2. **Master DeleteFile**：实现 DeleteFile，删除 inode 并记录 pending_orphan_inodes
3. **Master Heartbeat 调度**：后台线程周期性调用各 Worker 的 Heartbeat，下发 orphan/misplaced
4. **MetaStore ScanPaginated**：新增分页遍历接口，避免 ScanAll 全量
5. **Worker Heartbeat 处理**：解析 Request，执行 orphan/misplaced 清理，填充 Response audit 字段
6. **Worker GC 逻辑**：GcReconciler 或内联逻辑，分页遍历 + HashRing 归属判断
7. **集成测试**：gc_reconciliation_test.cpp，可控 ring、假 MetaStore、DeleteFile→对账→验证

## 文件路径

| 文件 | 变更类型 |
|------|----------|
| src/proto/worker.proto | 修改 |
| src/master/master_service_impl.* | 修改（DeleteFile、Heartbeat 调度） |
| src/master/master_server.* | 修改（启动 Heartbeat 线程） |
| src/worker/meta/meta_store.h/.cpp | 修改（ScanPaginated） |
| src/worker/worker_service_impl.* | 修改（Heartbeat 处理） |
| src/worker/page/page_store.* | 可选（已有 DeleteBlockPages） |
| tests/integration/gc_reconciliation_test.cpp | 新增 |
| CMakeLists.txt | 修改（添加测试） |

## 验收标准

- [ ] 删除文件后，对账可清理对应缓存页
- [ ] 拓扑变化后，对账可清理不再归属当前 Worker 的 block
- [ ] 对账过程不会阻塞正常读写主路径
- [ ] 测试使用可控 ring 变化和假 MetaStore 数据

## Risks

- Master 需能连接 Worker（Worker 的 host/port 来自 RegisterWorker）
- 单 Worker 场景下 misplaced 恒为空，需多 Worker 或 mock ring 验证
