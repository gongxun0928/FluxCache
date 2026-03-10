# P2-08: ChannelPool 完整实现与幂等重试边界

## 阶段与优先级

Phase E — 多 Worker 与传输增强 | P1

## 依赖

- [P1-04](./P1-04-channel-pool-minimal.md)
- [P1-14D](./P1-14d-read-path-e2e.md)
- [P1-15D](./P1-15d-write-path-e2e.md)

## 描述

在最小连接复用之上补齐生产前必需的 transport 能力，但保持“只对幂等操作自动重试”的边界。

## 交付物

- 每地址多 Channel 池
- 轮询或等价负载分发
- 连接预热
- 基础健康检查
- `retry_policy`

## 验收标准

- [ ] 池大小可配置。
- [ ] 幂等读请求在网络抖动时可自动重试。
- [ ] 非幂等写请求默认不自动重试。
- [ ] 不可用 Channel 可被剔除并重建。
- [ ] 测试通过伪 transport 或错误注入验证重试边界。

## 涉及目录

```text
src/common/rpc/
tests/common/
```
