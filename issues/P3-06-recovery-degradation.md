# P3-06: 安全恢复与受限降级策略

## 阶段与优先级

Phase I — HA 与弹性 | P0

## 依赖

- [P2-05B](./P2-05b-worker-gc-reconciliation.md)
- [P3-05](./P3-05-resilience-config.md)
- [P2-10](./P2-10-master-ha-raft.md)

## 描述

实现不破坏 `write-through` 正确性边界的恢复与降级策略。

本 issue 明确排除以下高风险承诺：

- 不支持 `cache-only write`
- 不支持“Master 不可用时的通用直连 Worker”
- 不支持未持久化写入排队后补交

允许的安全范围：

- UFS 慢但已有缓存时，读请求可返回带显式 `stale` 标记的受限结果
- Worker 抖动时，Client 可基于最新 ring 做幂等读重试
- Worker 恢复后通过 MetaStore 恢复索引并重新注册

## 交付物

- [安全降级设计](../design/degradation-policy-design.md) 对应实现
- stale 读结果或等价显式状态传播
- 降级指标导出
- 故障注入测试

## 验收标准

- [ ] UFS 超时但缓存存在时，系统只在显式降级模式下返回可识别的 stale 结果。
- [ ] 写请求在 UFS 不可达时返回失败，不进入隐式排队。
- [ ] Worker 下线后，幂等读请求可刷新 ring 并重试到可用节点。
- [ ] 降级模式有明确 metrics 和日志标记。
- [ ] 故障注入测试覆盖“慢 UFS”“Worker 下线”“Worker 恢复”三类场景。

## 涉及目录

```text
src/client/
src/master/
src/worker/
tests/integration/
```
