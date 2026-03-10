# P1-15D: 写路径端到端验证

## 阶段与优先级

Phase C — 数据面闭环 | P1

## 依赖

- [P1-14D](./P1-14d-read-path-e2e.md)
- [P1-15C](./P1-15c-client-write-path.md)

## 描述

验证 Phase 1 的 write-through 主路径已经正确拼装：Client 写入 Worker，Worker 落盘 UFS，Master 更新元数据，随后读路径可以读取到新内容。

## 交付物

- `tests/integration/write_path_e2e_test.cpp`

## 验收标准

- [ ] 写入后通过读路径可以返回新内容。
- [ ] 写入后直接读取 UFS 文件可看到持久化结果。
- [ ] UFS 注入失败时，E2E 返回错误且不留下伪成功状态。
- [ ] 小尺寸测试配置覆盖单页、跨页、跨 block 三类边界。

## 涉及目录

```text
tests/integration/
```
