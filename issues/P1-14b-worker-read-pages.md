# P1-14B: Worker.ReadPages 最小实现

## 阶段与优先级

Phase C — 数据面闭环 | P1

## 依赖

- [P1-06](./P1-06-worker-skeleton.md)
- [P1-10](./P1-10-page-store.md)
- [P1-11](./P1-11-ufs-localfs.md)

## 描述

实现 Worker 侧的最小读路径：按 `block_id + page_indices` 查 PageStore，`mtime` 不匹配或未命中时使用 `ufs_uri + ufs_path` 回源，再回填缓存。

## 交付物

- `ReadPages` RPC 真实实现
- 读缓存命中路径
- 读 UFS 回源路径
- 回填 PageStore
- 返回页数据和命中/未命中统计钩子

## 验收标准

- [ ] 首次读取未命中时会回源 UFS 并写入 PageStore。
- [ ] 再次读取相同页且 `expected_mtime_ms` 一致时命中缓存。
- [ ] `expected_mtime_ms` 不匹配时会淘汰旧页并重新回源。
- [ ] 测试通过 `FakeUfs.read_count` 或等价计数器验证命中与回源，而非只看日志。

## 涉及目录

```text
src/worker/
tests/worker/
```
