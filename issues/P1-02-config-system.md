# P1-02: 配置加载系统（YAML）

## 阶段与优先级

Phase A — 契约冻结 | P1

## 依赖

- [P1-01](./P1-01-project-scaffolding.md)

## 描述

实现统一配置加载框架，供 Master、Worker、Client 共享使用。

Phase 1 重点是为 MVP 主线提供稳定配置契约，而不是一次性囊括所有未来高级特性配置。

## 交付物

- `src/common/config/`
- `MasterConfig`
- `WorkerConfig`
- `ClientConfig`
- `UfsConfig`
- `config/fluxcache.yaml.example`

## 验收标准

- [ ] YAML 文件可成功加载到结构体。
- [ ] 缺失必填字段时输出明确错误。
- [ ] 配置模板与当前 MVP 主线保持一致，不提前要求 S3/HDFS/FUSE/HA 专属字段。
- [ ] 单元测试覆盖正常加载、缺字段、类型错误三类场景。

## 涉及目录

```text
src/common/config/
config/
tests/common/
```
