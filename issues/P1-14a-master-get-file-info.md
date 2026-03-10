# P1-14A: Master.GetFileInfo 最小实现

## 阶段与优先级

Phase C — 数据面闭环 | P1

## 依赖

- [P1-05B](./P1-05b-inode-store-and-tree.md)
- [P1-05C](./P1-05c-worker-manager-and-hash-ring.md)
- [P1-08B](./P1-08b-sync-from-ufs-and-unmount-safety.md)
- [P1-11](./P1-11-ufs-localfs.md)

## 描述

实现 `Master.GetFileInfo`，为 Client 提供最小稳定元数据合同：`inode_id`、`size`、`block_size`、`ufs_mtime_ms`、`ring_version`、`workers`、`ufs_uri`、`ufs_path`。

## 交付物

- `GetFileInfo` 真实 RPC 实现
- 挂载解析
- `path -> inode_id` 查询
- inode 缺失时的 UFS 同步补齐
- UFS `GetStatus` 获取 `mtime`

## 验收标准

- [ ] 对存在文件返回完整 `FileInfo` 和传输所需字段。
- [ ] 对不存在路径返回明确 `NOT_FOUND` 类错误。
- [ ] `workers` 和 `ring_version` 与当前 WorkerManager 状态一致。
- [ ] 测试使用可控 UFS 桩验证 `ufs_mtime_ms` 被正确透出。

## 涉及目录

```text
src/master/
tests/master/
```
