# Todo: P1-06 Worker 进程骨架与心跳

> Plan: [plan_p1-06-worker-skeleton.md](plan_p1-06-worker-skeleton.md)

## Todo 列表

| ID | 任务 | 状态 | DoD |
|----|------|------|-----|
| T1 | 扩展 WorkerConfig (host, port, heartbeat_interval_ms) | completed | config.h/cpp 更新，LoadConfig 解析新字段 |
| T2 | 实现 worker_server.h/.cpp | completed | WorkerServer Start/Shutdown，gRPC 绑定 |
| T3 | 实现 worker_service_impl.h/.cpp | completed | ReadPages/WritePages UNIMPLEMENTED，Heartbeat 空响应 |
| T4 | 实现 main.cpp | completed | --config 解析、信号处理、WorkerServer+心跳生命周期 |
| T5 | 实现心跳客户端骨架 | completed | 后台线程按周期调用 Master.RegisterWorker |
| T6 | 扩展 Master.RegisterWorker 幂等 | completed | endpoint.worker_id>0 时刷新并返回 |
| T7 | 添加 fluxcache-worker 可执行目标 | completed | CMakeLists 添加 executable |
| T8 | 更新 config 示例 | completed | config.yaml、example.yaml 含 worker.host/port/heartbeat_interval_ms |
| T9 | 添加 tests/worker/ 测试 | completed | 启动、心跳、Master 不可达、优雅停机 |
| T10 | 构建与测试 | completed | cmake .. && cmake --build . && ctest 通过 |

## 执行顺序

T1 → T2 → T3 → T4 → T5 → T6 → T7 → T8 → T9 → T10

## 约束

- 全程最多一个 in_progress
- DoD 可验证
