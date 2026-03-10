# Plan: P1-05C WorkerManager 与 HashRingManager 最小实现

> Status: Done

## Goal

- **Problem**: Master 当前仅维护简单 Worker 列表，无状态机、无一致性哈希，无法满足 Block 路由与 Client 缓存同步需求。
- **Target outcome**: 实现 WorkerManager（注册、心跳、ALIVE/SUSPECT/DEAD 状态机）与 HashRingManager（一致性哈希、GetWorker/GetCandidates/GetRingSnapshot），集成到 MasterServiceImpl，通过验收标准。

## 依赖

- P1-05A（Master 启动骨架）
- P1-16（Core types：BlockId、WorkerId、WorkerState）

## Steps

1. **WorkerManager**：`src/master/worker_manager.h/.cpp`
   - `WorkerInfo` 结构体：worker_id、host、port、state、last_heartbeat_ms
   - `RegisterWorker(WorkerId, host, port)`：新 Worker 进入 ALIVE
   - `HandleHeartbeat(WorkerId, now_ms)`：更新 last_heartbeat
   - `CheckWorkerHealth(now_ms, heartbeat_timeout_ms, suspect_grace_ms)`：驱动状态转换，返回新进入 DEAD 的 WorkerId 列表
   - `GetWorker(WorkerId)` / `GetWorkerState(WorkerId)` / `GetAllWorkersInRing()`：供 HashRingManager 与 GetHashRing 使用
   - 超时参数可注入，便于单元测试（可控时钟）

2. **HashRingManager**：`src/master/hash_ring_manager.h/.cpp`
   - 一致性哈希环（虚拟节点 150/Worker）
   - `AddWorker(WorkerId)` / `RemoveWorker(WorkerId)`：拓扑变更时 `ring_version++`
   - `GetWorker(BlockId)`：确定性路由到第一个 ALIVE 节点
   - `GetCandidates(BlockId, n, skip_suspect)`：返回前 n 个 ALIVE（默认跳过 SUSPECT）
   - `GetRingSnapshot()`：返回环上所有 Worker（ALIVE+SUSPECT）的 WorkerEndpoint 列表及状态，供 Client 同步
   - 依赖 WorkerManager 提供 Worker 列表与状态

3. **MasterServiceImpl 集成**
   - 持有 `WorkerManager` 与 `HashRingManager`
   - `RegisterWorker`：调用 WorkerManager.RegisterWorker，HashRingManager.AddWorker，ring_version 由 HashRingManager 管理
   - `GetHashRing`：从 HashRingManager.GetRingSnapshot 获取 workers 与 ring_version
   - 预留 `HandleHeartbeat` RPC 与 `CheckWorkerHealth` 定时调用（本 issue 可先不实现 Heartbeat RPC，仅实现 WorkerManager 接口）

4. **Proto 扩展**（如需 Client 区分 SUSPECT）
   - 在 `GetHashRingResponse` 增加 `map<uint64, int32> worker_states`（worker_id -> WorkerState 枚举值），便于 Client 路由时跳过 SUSPECT

5. **单元测试**：`tests/master/worker_manager_test.cpp` 或 `hash_ring_test.cpp`
   - 注册后 GetHashRing workers.size()==1
   - 增删 Worker 时 ring_version 递增
   - 相同 BlockId 拓扑不变时确定性路由
   - SUSPECT 保留在 ring，GetCandidates 默认跳过
   - 状态机：注入超时参数验证 ALIVE→SUSPECT→DEAD

## Risks & Assumptions

- **Risk**: HashRingManager 与 WorkerManager 的协作顺序（先 WorkerManager 再 HashRingManager）需在协调层明确，避免锁嵌套。
- **Assumption**: 单 Master 场景，无并发 Register；CheckWorkerHealth 由外部定时调用，本 issue 可仅实现接口。
- **Assumption**: 一致性哈希使用 std::hash<BlockId> 或类似，虚拟节点 key 为 `worker_id + "#" + vnode_index`。

## To Confirm

- [ ] 验收标准以 `issues/P1-05c-worker-manager-and-hash-ring.md` 为准。
- [ ] GetHashRing 返回的 workers 包含 ALIVE+SUSPECT（DEAD 已移出环），若需 Client 跳过 SUSPECT 则需 worker_states。

## 变更分级

P1 — 单服务核心逻辑、公共接口（WorkerManager、HashRingManager）。

## 测试设计

| 场景 | 验证方式 |
|------|----------|
| 注册后 GetHashRing workers.size()==1 | 单元测试 |
| 增删 Worker 时 ring_version 递增 | 单元测试 |
| 相同 BlockId 确定性路由 | 单元测试 |
| SUSPECT 在 ring 中、GetCandidates 跳过 | 单元测试 |
| 状态机 ALIVE→SUSPECT→DEAD | 注入 now_ms、timeout 参数验证 |
