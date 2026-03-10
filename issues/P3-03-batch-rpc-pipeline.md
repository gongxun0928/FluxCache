# P3-03: 批量 RPC 与顺序读 Pipeline

## 阶段与优先级

Phase J — 性能、可观测与质量 | P1

## 依赖

- [P1-14D](./P1-14d-read-path-e2e.md)
- [P2-08](./P2-08-channel-pool-full.md)

## 描述

针对顺序读场景减少 RPC 往返成本，引入批量读取和 pipeline/预读能力。

## 交付物

- 批量读取协议扩展
- Client 顺序读预读逻辑
- Worker 批量读取支持
- 顺序读基准

## 验收标准

- [ ] 批量读取结果与逐页读取一致。
- [ ] 顺序读吞吐有明确提升。
- [ ] 随机读不会因预读显著退化。
- [ ] 基准和回归测试通过。

## 涉及目录

```text
src/proto/
src/client/
src/worker/
tests/benchmark/
```
