# P1-14C: Client.Read 最小实现

## 阶段与优先级

Phase C — 数据面闭环 | P1

## 依赖

- [P1-07](./P1-07-client-skeleton.md)
- [P1-14A](./P1-14a-master-get-file-info.md)
- [P1-14B](./P1-14b-worker-read-pages.md)
- [P1-16](./P1-16-core-types.md)

## 描述

实现 Client 侧读路径：调用 `GetFileInfo`，按 `BlockId` 和 `PageId` 切片，本地选择 Worker，发送 `ReadPages` 并拼接返回结果。

本 issue 只实现文件级主路径，不在此处引入 SDK L1 缓存或批量 pipeline 优化。

## 交付物

- `FluxCacheClient::Read`
- 跨 page 切分
- 跨 block 切分
- 失败时按最新 `ring_version` 刷新一次后重试

## 验收标准

- [ ] 单页内读取返回正确数据。
- [ ] 跨页读取返回正确裁剪结果。
- [ ] 跨 block 读取返回正确拼接结果。
- [ ] 测试使用小尺寸配置（如 `block_size = 2MB`、`page_size = 1MB`）验证边界，而非依赖超大文件。

## 涉及目录

```text
src/client/
tests/client/
```
