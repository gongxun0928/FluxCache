# P1-06: Worker 进程骨架与心跳

## 阶段与优先级

Phase C — 数据面闭环 | P1

## 依赖

- [P1-02](./P1-02-config-system.md)
- [P1-03](./P1-03-grpc-proto.md)
- [P1-16](./P1-16-core-types.md)

## 描述

实现 Worker 进程的最小宿主：配置加载、gRPC Server、心跳循环和优雅停机。

本 issue 不实现真实读写逻辑，只提供后续 `PageStore` 和 `ReadPages/WritePages` 所需宿主环境。

## 交付物

- `src/worker/main.cpp`
- `src/worker/worker_server.h/.cpp`
- `src/worker/worker_service_impl.h/.cpp`
- 心跳客户端骨架

## 验收标准

- [ ] `./fluxcache-worker --config config.yaml` 可启动并监听配置端口。
- [ ] 心跳线程会按配置周期尝试上报。
- [ ] Master 不可达时输出稳定错误并持续运行，不崩溃。
- [ ] `SIGINT` / `SIGTERM` 可触发优雅停机。

## 涉及目录

```text
src/worker/
tests/worker/
```
