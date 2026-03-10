# P1-05A: Master 进程启动与服务注册骨架

## 阶段与优先级

Phase B — 元数据面闭环 | P1

## 依赖

- [P1-02](./P1-02-config-system.md)
- [P1-03](./P1-03-grpc-proto.md)
- [P1-16](./P1-16-core-types.md)

## 描述

搭建 Master 进程的最小可运行外壳，只负责配置加载、gRPC Server 生命周期、服务注册和优雅停机，不在本 issue 中实现真实元数据逻辑。

本 issue 的目标是给后续 `InodeTree`、`MountTable`、`WorkerManager` 和 `HashRingManager` 提供稳定宿主。

## 交付物

- `src/master/main.cpp`
- `src/master/master_server.h/.cpp`
- `src/master/master_service_impl.h/.cpp`
- 配置驱动的监听地址、日志初始化、信号处理
- 最小 `GetHashRing` 响应：`ring_version = 0`、`workers = []`
- 最小 `RegisterWorker` 响应：接受请求并返回成功/明确错误

## 验收标准

- [ ] `./fluxcache-master --config config.yaml` 可启动并监听配置端口。
- [ ] `GetHashRing` 在无 Worker 时返回可观测最小行为，而非笼统 `UNIMPLEMENTED`。
- [ ] `RegisterWorker` 可接收合法请求并返回稳定响应。
- [ ] 配置缺失时输出明确错误并退出。
- [ ] `SIGINT` / `SIGTERM` 可触发优雅停机。

## 涉及目录

```text
src/master/
tests/master/
```
