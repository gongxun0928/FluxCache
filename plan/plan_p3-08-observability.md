# Plan: P3-08 可观测性指标扩展

> Status: Done

## Goal

- **Problem**: 基础 `/metrics` 仅有 RPC 计数与延迟，缺少与缓存层级、UFS 回源、弹性策略直接相关的指标。
- **Target outcome**: 在 `/metrics` 中可见 tier hit/miss、promotion/eviction、UFS read、client retry/breaker、active workers；指标值与实际行为一致；热路径采用低开销计数。

## 依赖

- P1-13 (Metrics 基础)
- P2-02 (TierPromoter/TierEvictor)
- P3-05 (Resilience 配置)

## Steps

1. **扩展 MetricsRegistry** (`src/common/metrics/`)
   - 新增 `IncCounter(name, labels)`：支持自定义 counter 名称与 label（如 `tier_hit{tier="memory"}`）
   - 新增 `SetGauge(name, labels, value)`：用于 active_workers
   - 新增 `ObserveHistogram(name, labels, value)`：用于 UFS latency（复用现有 bucket 逻辑）
   - 热路径使用 `std::atomic` + `memory_order_relaxed`，避免锁竞争

2. **PageStore 埋点** (`src/worker/page/page_store.cpp`)
   - GetPage 成功：按 tier 递增 `fluxcache_tier_hits_total{tier="memory|ssd|hdd"}`
   - GetPage NotFound：递增 `fluxcache_tier_misses_total`
   - 需传入 `MetricsRegistry*`（可选，构造时注入）

3. **TierPromoter / TierEvictor 埋点**
   - TierPromoter::PromoteOne 成功：`fluxcache_promotions_total`
   - TierEvictor::EvictOne 成功：区分 demotion 与 eviction，`fluxcache_evictions_total{type="demotion|eviction"}`
   - 构造时注入 `MetricsRegistry*`（可选）

4. **Worker 埋点**
   - WorkerServiceImpl：UFS Read 前记录 start，成功后 `fluxcache_ufs_reads_total`、`ObserveHistogram(ufs_read_latency_seconds)`
   - 将 TierPromoter/TierEvictor 接入 WorkerServer 后台循环（当 data_dir 配置时），定期调用 PromoteOne/EvictOne，使 promotion/eviction 指标可观测

5. **Master 埋点**
   - 在 ExportPrometheus 时计算 `fluxcache_active_workers` = WorkerManager::GetAllWorkersInRing().size()（或新增 GetAliveCount）

6. **Client 埋点**
   - 扩展 ClientConfig：`metrics_port`（0=禁用）
   - FluxCacheClient 可选创建 MetricsRegistry + HttpMetricsServer
   - ExecuteWithRetry：每次重试递增 `fluxcache_client_retries_total{service="master|worker"}`
   - CircuitBreaker：RecordFailure 时若状态变为 Open，递增 `fluxcache_client_breaker_opens_total`
   - MasterClient/WorkerClient 需接收 MetricsRegistry* 并传入 ExecuteWithRetry 包装

7. **ExportPrometheus 扩展**
   - 导出所有新增 counter、gauge、histogram

## 指标清单

| 指标名 | 类型 | 说明 |
|--------|------|------|
| fluxcache_tier_hits_total | counter | 按 tier 的 cache hit |
| fluxcache_tier_misses_total | counter | cache miss |
| fluxcache_promotions_total | counter | 晋升成功次数 |
| fluxcache_evictions_total | counter | 淘汰次数，label type=demotion|eviction |
| fluxcache_ufs_reads_total | counter | UFS 回源读次数 |
| fluxcache_ufs_read_duration_seconds | histogram | UFS 读延迟 |
| fluxcache_client_retries_total | counter | 客户端重试次数 |
| fluxcache_client_breaker_opens_total | counter | 熔断器打开次数 |
| fluxcache_active_workers | gauge | Master 视角活跃 Worker 数 |

## 测试设计

- 单元测试：MetricsRegistry 新增 API 的计数/gauges 正确
- 集成：启动 Worker，触发 ReadPages（含 UFS miss），curl /metrics 验证 tier_miss、ufs_reads 递增
- 集成：启动 Master，注册 Worker，curl /metrics 验证 active_workers
- 集成：Client 配置 metrics_port，触发重试/熔断，验证 retries/breaker_opens

## Risks & Assumptions

- **Risk**: 热路径原子操作可能仍有 cache line 竞争。**Mitigation**: 使用 per-stripe 或 per-thread 局部计数，定期合并（本阶段先用单一 atomic，若压测有问题再优化）。
- **Assumption**: Client metrics 需 metrics_port 配置才暴露；未配置时仅记录到内存，不启动 HTTP。
- **Assumption**: TierPromoter/TierEvictor 接入 Worker 后台循环，默认间隔 5s，可后续配置化。

## To Confirm

- [x] 变更分级 P2
- [x] 验收标准以 issues/P3-08-observability-expansion.md 为准

## 变更分级

P2 — 可观测扩展、非核心逻辑变更。
