# P1-05B: InodeStore 与 InodeTree 最小持久化

## 阶段与优先级

Phase B — 元数据面闭环 | P1

## 依赖

- [P1-05A](./P1-05a-master-server-bootstrap.md)
- [P1-11](./P1-11-ufs-localfs.md)

## 描述

实现 Phase 1 的元数据 source of truth：基于 RocksDB 的 `InodeStore` 与最小 `InodeTree`。

本 issue 只解决 `path <-> inode_id`、目录骨架、基础文件属性持久化，以及通过 UFS 补齐缺失 inode 的最小机制。

## 交付物

- `src/master/inode_store.h/.cpp`
- `src/master/inode_tree.h/.cpp`
- `inodes` / `edges` 两个 Column Family
- 根目录初始化与恢复
- `LookupPath`
- `CreateFile`
- `CreateDirectory`
- `DeleteInode`
- `ListDirectory`

## 验收标准

- [ ] 首次启动时可创建根目录并持久化。
- [ ] 重启后可从 RocksDB 恢复根目录与已存在 inode。
- [ ] `LookupPath` 对已创建路径返回稳定的 `inode_id`。
- [ ] `CreateFile` / `CreateDirectory` 会写入 `inodes` 和 `edges`。
- [ ] `DeleteInode` 采用立即删除语义，不依赖 `MarkDeleted`。
- [ ] 测试使用临时目录 RocksDB，验证重启恢复而非仅内存行为。

## 涉及目录

```text
src/master/
tests/master/
```
