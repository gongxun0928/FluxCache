# P1-15B: Worker.WritePages 最小实现

## 阶段与优先级

Phase C — 数据面闭环 | P1

## 依赖

- [P1-10](./P1-10-page-store.md)
- [P1-11](./P1-11-ufs-localfs.md)
- [P1-14B](./P1-14b-worker-read-pages.md)

## 描述

实现 Worker 侧最小写路径，遵循 Phase 1 的 `write-through` 语义：先写 UFS，再更新 PageStore。

## 交付物

- `WritePages` RPC 真实实现
- UFS 写入
- 写后更新缓存页与 `mtime`
- 失败时的最小回滚/清理逻辑

## 验收标准

- [ ] UFS 写入成功后才更新 PageStore。
- [ ] UFS 写入失败时返回错误且不留下伪成功缓存。
- [ ] 同一页写入后，再次读取能拿到新内容和新 `mtime`。
- [ ] 测试使用假 UFS 注入失败验证原子边界。

## 涉及目录

```text
src/worker/
tests/worker/
```
