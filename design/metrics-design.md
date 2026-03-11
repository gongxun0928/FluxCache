# Metrics 基础导出设计

> 日期: 2026-03-11
> 状态: Phase 1（P1-13）
> 关联: [P1-13](../issues/P1-13-metrics-basic.md)

## 1. 概述

提供最小 `/metrics` 导出能力，输出 Prometheus 文本格式，为后续可观测扩展提供统一入口。Master 和 Worker 可独立暴露 metrics 端口，RPC 处理中记录请求计数与延迟。

## 2. 设计决策

### 2.1 格式与协议

- **Prometheus Text Format**：`text/plain; version=0.0.4`，UTF-8，`\n` 换行
- **HTTP 端点**：`GET /metrics` 返回 200，Content-Type 正确
- **指标类型**：Counter（请求计数）、Histogram（延迟，秒）

### 2.2 模块边界

| 模块 | 职责 |
|------|------|
| `MetricsRegistry` | 线程安全计数器/直方图注册与快照，输出 Prometheus 文本 |
| `HttpMetricsServer` | 独立 HTTP 服务，监听 metrics_port，仅处理 GET /metrics |

### 2.3 指标命名

- 前缀：`fluxcache_`
- 请求计数：`fluxcache_rpc_requests_total{service="master",method="GetFileInfo"}`
- 延迟：`fluxcache_rpc_duration_seconds{service="master",method="GetFileInfo"}`（histogram，含 `_bucket`、`_sum`、`_count`）

### 2.4 HTTP 服务实现

- **方案**：使用 cpp-httplib（header-only）或自实现最小 HTTP 解析
- **选择**：自实现最小 HTTP 服务（避免新依赖），仅支持 `GET /metrics`，返回固定 Content-Type 与 body

### 2.5 配置

- `MasterConfig.metrics_port`：0 表示不启动 metrics 服务
- `WorkerConfig.metrics_port`：同上
- 可选字段，默认 0

### 2.6 非目标（Phase 1）

- 复杂 histogram 分桶配置
- 非 /metrics 路径
- 认证、TLS

## 3. 接口设计

```cpp
// metrics/metrics_registry.h
class MetricsRegistry {
 public:
  void IncCounter(const std::string& service, const std::string& method);
  void ObserveLatency(const std::string& service, const std::string& method, double seconds);
  std::string ExportPrometheus() const;  // Prometheus text format
};

// metrics/http_metrics_server.h
class HttpMetricsServer {
 public:
  explicit HttpMetricsServer(uint16_t port, MetricsRegistry* registry);
  bool Start();   // 后台线程
  void Shutdown();
};
```

## 4. 测试设计

| 场景 | 输入 | 预期 |
|------|------|------|
| MetricsRegistry 计数 | IncCounter("master","GetFileInfo") x3 | ExportPrometheus 含 `fluxcache_rpc_requests_total{service="master",method="GetFileInfo"} 3` |
| MetricsRegistry 延迟 | ObserveLatency("worker","ReadPages", 0.05) | ExportPrometheus 含 `fluxcache_rpc_duration_seconds` 相关行 |
| HttpMetricsServer 启动 | port=0（随机） | Start() 返回 true |
| curl /metrics | GET http://localhost:{port}/metrics | 200，Content-Type 含 text/plain，body 为 Prometheus 格式 |
| RPC 后计数递增 | 调用 Master.GetFileInfo | /metrics 中对应 counter 递增 |

## 5. 风险与假设

- **风险**：自实现 HTTP 解析有边界情况。**Mitigation**：仅解析 GET /metrics，其他请求返回 404。
- **假设**：metrics_port=0 时 HttpMetricsServer 不启动，Master/Worker 行为不变。
