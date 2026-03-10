# P1-14D: 读路径端到端验证

## 阶段与优先级

Phase C — 数据面闭环 | P1

## 依赖

- [P1-14C](./P1-14c-client-read-path.md)

## 描述

使用单 Master、单 Worker、LocalFS UFS 组成最小集群，验证 Phase 1 读路径已被正确拼装。

本 issue 不再补做核心功能；前提是假设 `GetFileInfo`、`ReadPages`、`Client.Read` 已各自通过单测。

## 交付物

- `tests/integration/read_path_e2e_test.cpp`
- 启动最小集群或等价测试夹具
- E2E 读路径验证

## 验收标准

- [ ] 读取 UFS 中已有文件返回正确内容。
- [ ] 首次读取后 `FakeUfs.read_count` 增长。
- [ ] 再次读取相同页且版本未变时 `FakeUfs.read_count` 不再增长。
- [ ] 文件不存在时返回明确错误。

## 涉及目录

```text
tests/integration/
```
