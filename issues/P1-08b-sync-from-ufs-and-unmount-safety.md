# P1-08B: SyncFromUfs 与 Unmount 安全规则

## 阶段与优先级

Phase B — 元数据面闭环 | P1

## 依赖

- [P1-05B](./P1-05b-inode-store-and-tree.md)
- [P1-08A](./P1-08a-mount-table-core.md)
- [P1-11](./P1-11-ufs-localfs.md)

## 描述

补齐 `MountTable` 与 `InodeTree` 之间的真实边界：当路径命中挂载点但 inode 不存在时，允许通过 UFS 元数据补齐；同时定义卸载前的安全检查。

## 交付物

- `SyncFromUfs(path)` 最小实现
- `GetFileInfo` / `ListDirectory` 可触发按需补齐 inode
- Unmount 安全检查：
  - 已解析但未持久化的 UFS 子项不得被静默忽略
  - 挂载点存在活跃 inode 或已发现的子项时拒绝卸载

## 验收标准

- [ ] UFS 中已存在、但 RocksDB 尚无记录的文件，可在首次访问时补齐 inode。
- [ ] `ListDirectory` 可返回补齐后的目录项。
- [ ] 卸载一个仍有活跃 inode 的挂载点时返回明确错误。
- [ ] 测试使用假 UFS 或临时目录验证“补齐前不存在、补齐后可解析”的过程。

## 涉及目录

```text
src/master/
tests/master/
```
