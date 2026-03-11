# Plan: P3-09 慢请求与热点页追踪

> Status: Done

## Goal

- **Problem**: 缺少调优和定位问题用的慢请求与热点页追踪能力。
- **Target outcome**: 超过阈值的 RPC 可被识别并记录；热点 PageId 可被识别；指标可在 `/metrics` 中查看；可通过 `/debug/slow-requests` 和 `/debug/hot-pages` 查询详情。

## 依赖

- P3-08 (可观测性指标扩展)

## Steps

1. **SlowRequestTracker** (`src/common/metrics/slow_request_tracker.h/.cpp`)
   - `Record(service, method, duration_sec, extra_info)`：当 duration > threshold 时记录
   - `GetRecentSlowRequests(max_count)`：返回最近慢请求列表，供 `/debug/slow-requests`
   - `ExportPrometheusFragment()`：导出 `fluxcache_slow_requests_total`、`fluxcache_slow_request_duration_seconds` 到 Prometheus
   - 使用固定大小 ring buffer（如 100 条），低锁竞争
   - 默认阈值 1.0 秒，可配置

2. **HotspotTracker** (`src/worker/cache/hotspot_tracker.h/.cpp`)
   - `Record(PageId)`：递增访问计数
   - `GetTopK(k)`：返回 Top-K 热点页
   - 使用 `unordered_map<PageId, uint64_t>` + 定期取 Top-K，或 min-heap 维护 Top-K
   - Top-K 默认 100，可配置

3. **扩展 HttpMetricsServer**
   - 支持 `GET /debug/slow-requests`：返回 SlowRequestTracker 的文本列表
   - 支持 `GET /debug/hot-pages`：返回 HotspotTracker 的 Top-K 文本列表
   - 构造时注入可选的 `SlowRequestTracker*`、`HotspotTracker*`（Master 无 HotspotTracker，Client 无 HotspotTracker）

4. **MetricsRegistry 集成**
   - 新增 `SetSlowRequestTracker(SlowRequestTracker*)`、`SetHotspotTracker(HotspotTracker*)`（仅 Worker 需要 HotspotTracker）
   - `ExportPrometheus()` 时追加 slow request 与 hot page 相关指标

5. **Worker 埋点**
   - WorkerServer 创建 SlowRequestTracker、HotspotTracker，注入 MetricsRegistry 与 HttpMetricsServer
   - WorkerServiceImpl 构造时接收 `SlowRequestTracker*`、`HotspotTracker*`
   - RpcMetricsGuard 析构时：若 duration > threshold，调用 `slow_tracker->Record("worker", method, duration, block_id_info)`
   - ReadPages/BatchReadPages 中，对每个 `page_indices` 的 PageId 调用 `hotspot_tracker->Record(PageId)`

6. **Client 埋点**
   - FluxCacheClient 创建 SlowRequestTracker（当 metrics_port > 0 时），注入 MetricsRegistry
   - WorkerClient::ReadPages/BatchReadPages：在调用前后计时，若总耗时 > threshold，调用 `slow_tracker->Record("client_worker", "ReadPages", duration, "")`

## 指标清单

| 指标名 | 类型 | 说明 |
|--------|------|------|
| fluxcache_slow_requests_total | counter | 超过阈值的慢请求总数 |
| fluxcache_slow_request_duration_seconds | histogram | 慢请求延迟分布（仅慢请求样本） |
| fluxcache_hot_page_access_total | counter | 热点页 Record 调用总次数 |
| fluxcache_hot_pages_tracked | gauge | 当前追踪的独立 PageId 数量 |

## 测试设计

- 单元测试：SlowRequestTracker 阈值判断、GetRecent 顺序与内容
- 单元测试：HotspotTracker Record + GetTopK 正确性
- 单元测试：HttpMetricsServer `/debug/slow-requests`、`/debug/hot-pages` 返回格式
- 集成：Worker ReadPages 触发慢请求与热点页，curl 验证

## Risks & Assumptions

- **Risk**: 热路径 Record 可能增加延迟。**Mitigation**: 使用 atomic 计数，ring buffer 写入加锁但仅在超过阈值时触发。
- **Assumption**: 慢请求阈值默认 1.0s，可通过配置扩展（本阶段可写死）。
- **Assumption**: HotspotTracker 仅 Worker 使用；Master/Client 不暴露 `/debug/hot-pages`。

## To Confirm

- [x] 变更分级 P2
- [x] 验收标准以 issues/P3-09-slow-request-hotpage.md 为准

## 变更分级

P2 — 可观测扩展、非核心逻辑变更。
