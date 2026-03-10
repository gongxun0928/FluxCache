# P3-09: 慢请求与热点页追踪

## 阶段与优先级

Phase J — 性能、可观测与质量 | P2

## 依赖

- [P3-08](./P3-08-observability-expansion.md)

## 描述

提供调优和定位问题用的慢请求与热点页追踪，不影响核心读写语义。

## 交付物

- `src/common/metrics/slow_request_tracker.h/.cpp`
- `src/worker/cache/hotspot_tracker.h/.cpp`
- `/debug/slow-requests`
- `/debug/hot-pages`

## 验收标准

- [ ] 超过阈值的请求会被记录。
- [ ] 可查询最近慢请求列表。
- [ ] 可查询热点页 Top-K。
- [ ] 对正常请求延迟影响可控并有说明。
- [ ] 单元测试通过。

## 涉及目录

```text
src/common/metrics/
src/worker/cache/
tests/worker/
```
# P3-09: 慢请求与热点页追踪

## 阶段与优先级

Phase J — 性能、可观测与质量 | P2

## 依赖

- [P3-08](./P3-08-observability-expansion.md)

## 描述

提供调优和定位问题用的慢请求与热点页追踪，不影响核心读写语义。

## 交付物

- `src/common/metrics/slow_request_tracker.h/.cpp`
- `src/worker/cache/hotspot_tracker.h/.cpp`
- `/debug/slow-requests`
- `/debug/hot-pages`

## 验收标准

- [ ] 超过阈值的请求会被记录。
- [ ] 可查询最近慢请求列表。
- [ ] 可查询热点页 Top-K。
- [ ] 对正常请求延迟影响可控并有说明。
- [ ] 单元测试通过。

## 涉及目录

```text
src/common/metrics/
src/worker/cache/
tests/worker/
```
