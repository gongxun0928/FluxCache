# P1-13: Metrics 基础导出

## 阶段与优先级

Phase J — 性能、可观测与质量 | P2

## 依赖

- [P1-05A](./P1-05a-master-server-bootstrap.md)
- [P1-06](./P1-06-worker-skeleton.md)

## 描述

提供最小 `/metrics` 导出能力，为后续观测扩展提供统一入口，但不阻塞读写 MVP 主链。

## 交付物

- `src/common/metrics/metrics_registry.h/.cpp`
- `src/common/metrics/http_metrics_server.h/.cpp`
- Master / Worker 基础请求计数与延迟

## 验收标准

- [ ] `curl http://localhost:{metrics_port}/metrics` 返回 Prometheus 文本。
- [ ] Master 和 Worker 可独立暴露 metrics 端口。
- [ ] 触发 RPC 后，对应计数器可观察递增。

## 涉及目录

```text
src/common/metrics/
src/master/
src/worker/
```
