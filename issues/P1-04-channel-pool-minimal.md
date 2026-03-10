# P1-04: ChannelPool 最小实现

## 阶段与优先级

Phase A — 契约冻结 | P1

## 依赖

- [P1-03](./P1-03-grpc-proto.md)

## 描述

实现最小可用的 gRPC Channel 复用能力。Phase 1 只要求每个目标地址缓存一个 Channel，避免每次 RPC 重建连接。

## 交付物

- `src/common/rpc/channel_pool.h/.cpp`
- `GetChannel(address)`
- 线程安全地址缓存

## 验收标准

- [ ] 相同地址多次获取返回同一 Channel 实例。
- [ ] 不同地址返回不同 Channel。
- [ ] 可被 Master/Worker stub 复用。
- [ ] 单元测试验证线程安全和地址缓存行为。

## 涉及目录

```text
src/common/rpc/
tests/common/
```
# P1-04: ChannelPool 最小实现

## 阶段与优先级

Phase A — 契约冻结 | P1

## 依赖

- [P1-03](./P1-03-grpc-proto.md)

## 描述

实现最小可用的 gRPC Channel 复用能力。Phase 1 只要求每个目标地址缓存一个 Channel，避免每次 RPC 重建连接。

## 交付物

- `src/common/rpc/channel_pool.h/.cpp`
- `GetChannel(address)`
- 线程安全地址缓存

## 验收标准

- [ ] 相同地址多次获取返回同一 Channel 实例。
- [ ] 不同地址返回不同 Channel。
- [ ] 可被 Master/Worker stub 复用。
- [ ] 单元测试验证线程安全和地址缓存行为。

## 涉及目录

```text
src/common/rpc/
tests/common/
```
