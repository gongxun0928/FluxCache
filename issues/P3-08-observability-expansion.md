# P3-08: 可观测性指标扩展

## 阶段与优先级

Phase J — 性能、可观测与质量 | P2

## 依赖

- [P1-13](./P1-13-metrics-basic.md)
- [P2-02](./P2-02-tier-promotion-eviction.md)
- [P3-05](./P3-05-resilience-config.md)

## 描述

在基础 `/metrics` 之上补齐与缓存层级、UFS 回源和弹性策略直接相关的指标。

## 交付物

- tier hit / miss 指标
- promotion / eviction 指标
- UFS read count / latency 指标
- client retry / breaker 指标
- active workers 指标

## 验收标准

- [ ] 所有新增指标可在 `/metrics` 中看到。
- [ ] 指标值与实际行为一致。
- [ ] 热路径计数采用低开销方式，不引入明显性能退化。

## 涉及目录

```text
src/common/metrics/
src/master/
src/worker/
src/client/
```
