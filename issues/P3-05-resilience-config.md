# P3-05: 弹性配置层（超时 / 重试 / 熔断）

## 阶段与优先级

Phase I — HA 与弹性 | P1

## 依赖

- [P2-08](./P2-08-channel-pool-full.md)

## 描述

在已有 transport 能力上补齐分层超时、受控重试和熔断配置，为后续安全降级提供基础设施。

## 交付物

- `src/common/rpc/resilience_config.h/.cpp`
- `src/common/rpc/circuit_breaker.h/.cpp`
- 配置项扩展

## 验收标准

- [ ] 不同操作类型可配置不同超时。
- [ ] 熔断器在错误率超阈值后可阻断请求。
- [ ] HALF_OPEN 探测成功后恢复正常。
- [ ] 重试、超时、熔断与 `P2-08` 的幂等边界保持一致。
- [ ] 单元测试通过。

## 涉及目录

```text
src/common/rpc/
tests/common/
```
# P3-05: 弹性配置层（超时 / 重试 / 熔断）

## 阶段与优先级

Phase I — HA 与弹性 | P1

## 依赖

- [P2-08](./P2-08-channel-pool-full.md)

## 描述

在已有 transport 能力上补齐分层超时、受控重试和熔断配置，为后续安全降级提供基础设施。

## 交付物

- `src/common/rpc/resilience_config.h/.cpp`
- `src/common/rpc/circuit_breaker.h/.cpp`
- 配置项扩展

## 验收标准

- [ ] 不同操作类型可配置不同超时。
- [ ] 熔断器在错误率超阈值后可阻断请求。
- [ ] HALF_OPEN 探测成功后恢复正常。
- [ ] 重试、超时、熔断与 `P2-08` 的幂等边界保持一致。
- [ ] 单元测试通过。

## 涉及目录

```text
src/common/rpc/
tests/common/
```
