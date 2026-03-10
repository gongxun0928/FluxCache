# P1-12: CLI smoke 工具

## 阶段与优先级

Phase G — 访问入口 | P2

## 依赖

- [P1-14D](./P1-14d-read-path-e2e.md)
- [P1-15D](./P1-15d-write-path-e2e.md)

## 描述

实现一个面向开发与联调的 smoke CLI，而不是试图在 Phase 1 提供完整运维入口。

## 交付物

- `src/client/cli/main.cpp`
- `mount`
- `unmount`
- `ls-mounts`
- `read`
- `write`
- `stat`

## 验收标准

- [ ] 在最小集群上可以完成一次 `write -> read -> stat` 验证。
- [ ] 集群不可达时输出明确错误。
- [ ] 只覆盖已存在服务端能力，不额外发明新 RPC。

## 涉及目录

```text
src/client/cli/
```
