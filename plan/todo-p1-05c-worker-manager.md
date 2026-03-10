# Todo: P1-05C WorkerManager 与 HashRingManager

> Plan: [plan_p1-05c-worker-manager.md](./plan_p1-05c-worker-manager.md)

## Todo 列表

| # | 任务 | 状态 | DoD |
|---|------|------|-----|
| 1 | 实现 worker_manager.h/.cpp | completed | RegisterWorker、HandleHeartbeat、CheckWorkerHealth、GetWorker、GetWorkerState、GetAllWorkersInRing；可注入超时参数 |
| 2 | 实现 hash_ring_manager.h/.cpp | completed | AddWorker、RemoveWorker、GetWorker、GetCandidates、GetRingSnapshot；ring_version 递增 |
| 3 | Proto 扩展 worker_states（可选） | cancelled | GetHashRingResponse 增加 map worker_states，便于 Client 跳过 SUSPECT |
| 4 | MasterServiceImpl 集成 | completed | 使用 WorkerManager+HashRingManager 替代当前 workers_ 向量 |
| 5 | 单元测试 worker_manager_test / hash_ring_test | completed | 覆盖验收标准 5 条 |
| 6 | 构建与 ctest 通过 | completed | cd build && cmake .. && cmake --build . && ctest -R worker -C Debug --output-on-failure |

## 约束

- 全程最多一个 `in_progress`
- DoD 可验证
