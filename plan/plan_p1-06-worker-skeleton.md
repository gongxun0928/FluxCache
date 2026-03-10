# Plan: P1-06 Worker 进程骨架与心跳

> Status: Done

## Goal

- **Problem**: Worker 进程尚无宿主环境，无法为后续 PageStore、ReadPages/WritePages 提供运行框架。
- **Target outcome**: `./fluxcache-worker --config config.yaml` 可启动并监听配置端口；心跳线程按配置周期向 Master 上报（调用 RegisterWorker）；Master 不可达时输出稳定错误并持续运行；SIGINT/SIGTERM 触发优雅停机。

## Design 摘要

### 架构

- **main.cpp**: 解析 `--config` → LoadConfig → 注册信号处理 → 创建 WorkerServer → 启动 gRPC 服务 → 启动心跳线程 → 等待停机信号 → 优雅关闭。
- **WorkerServer**: 持有 gRPC Server、WorkerServiceImpl；根据 WorkerConfig 绑定监听地址；提供 `Start()` / `Shutdown()`。
- **WorkerServiceImpl**: 实现 WorkerService 接口；ReadPages/WritePages 返回 UNIMPLEMENTED；Heartbeat 返回空响应（Phase 1 骨架）。
- **HeartbeatClient**: 独立线程，按 `heartbeat_interval_ms` 周期调用 `Master.RegisterWorker`；首次调用获取 worker_id，后续调用携带 worker_id 做幂等刷新；Master 不可达时输出稳定错误并继续运行。

### 配置扩展

- **WorkerConfig** 新增字段：
  - `host` (string): Worker 监听地址，默认 "0.0.0.0"
  - `port` (uint16): Worker 监听端口，必填
  - `heartbeat_interval_ms` (uint32): 心跳周期，默认 5000
- Master 地址复用 `FluxCacheConfig.master.host/port`。

### Master RegisterWorker 幂等

- 为支持心跳刷新，扩展 Master.RegisterWorker：当 `endpoint.worker_id > 0` 时，查找已有 worker 并刷新（更新 host/port 若变化），返回相同 worker_id；否则按原逻辑注册新 worker。

### 接口与契约

- 启动：`./fluxcache-worker --config config.yaml`
- Worker 监听：`worker.host:worker.port`
- 心跳：每 `heartbeat_interval_ms` 调用 Master.RegisterWorker；首次 endpoint.worker_id=0，后续携带已获 worker_id。
- Master 不可达：gRPC 调用失败时 stderr 输出错误，不崩溃，下一周期重试。
- 优雅停机：SIGINT/SIGTERM 设置标志 → 主循环退出 → Shutdown WorkerServer 和心跳线程。

### 测试设计

| 场景 | 输入 | 预期 |
|------|------|------|
| 正常启动 | 有效 config | 进程监听 worker.port，可接受连接 |
| 配置缺失 | 无 --config 或 worker.port 缺失 | 输出明确错误，退出码非 0 |
| 心跳周期 | Master 可达 | 按周期调用 RegisterWorker，无崩溃 |
| Master 不可达 | Master 未启动 | 输出稳定错误，进程持续运行 |
| 优雅停机 | SIGINT/SIGTERM | 进程正常退出，无崩溃 |

## Steps

1. 创建 `plan/todo-p1-06-worker-skeleton.md`。
2. 扩展 WorkerConfig：host、port、heartbeat_interval_ms；更新 LoadConfig。
3. 实现 `src/worker/main.cpp`：解析 --config、LoadConfig、信号处理、WorkerServer 生命周期、心跳线程。
4. 实现 `src/worker/worker_server.h/.cpp`：gRPC Server 封装、Start/Shutdown。
5. 实现 `src/worker/worker_service_impl.h/.cpp`：WorkerService 骨架（ReadPages/WritePages UNIMPLEMENTED，Heartbeat 空响应）。
6. 实现心跳客户端：在 main 或 WorkerServer 内启动后台线程，按周期调用 Master.RegisterWorker。
7. 扩展 Master.RegisterWorker 支持幂等（endpoint.worker_id 刷新）。
8. 修改 `src/worker/CMakeLists.txt`：添加 fluxcache-worker 可执行目标。
9. 更新 `config/config.yaml` 和 `config/example.yaml`：worker.host、worker.port、worker.heartbeat_interval_ms。
10. 添加 `tests/worker/` 下集成/单元测试。
11. 执行 `cd build && cmake .. && cmake --build . && ctest --output-on-failure`。
12. 单独 commit: `feat(worker): add Worker process skeleton and heartbeat`。

## Risks & Assumptions

- **Risk**: 心跳线程与主线程竞态。**Mitigation**: 使用 atomic bool 控制心跳循环退出，Shutdown 时设置标志并 join 线程。
- **Assumption**: Phase 1 使用 stderr 输出，不引入 spdlog。
- **Assumption**: Worker 的 host 默认 "0.0.0.0" 或从配置读取；port 必填。

## To Confirm

- [x] 验收标准以 issues/P1-06-worker-skeleton.md 为准。
- [x] 配置格式与 P1-02 LoadConfig 一致。

## 变更分级

P1 — 单服务核心逻辑、主流程稳定性。
