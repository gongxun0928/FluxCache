# P1-15A: Master.CreateFile / CompleteFile

## 阶段与优先级

Phase C — 数据面闭环 | P1

## 依赖

- [P1-05B](./P1-05b-inode-store-and-tree.md)
- [P1-08B](./P1-08b-sync-from-ufs-and-unmount-safety.md)

## 描述

实现写路径所需的两个元数据 RPC：`CreateFile` 和 `CompleteFile`。

它们分别负责分配 `inode_id`、建立最小文件元数据，以及在 write-through 成功后更新 `size` 与 `mtime`。

## 交付物

- `CreateFile` RPC
- `CompleteFile` RPC
- `size` / `mtime` 更新逻辑

## 验收标准

- [ ] `CreateFile` 返回稳定的 `inode_id` 与必要路径信息。
- [ ] `CompleteFile` 可更新 `size` 与 `mtime`。
- [ ] 再次调用 `GetFileInfo` 时可读到更新后的属性。
- [ ] 针对重复创建和非法路径返回明确错误。

## 涉及目录

```text
src/master/
tests/master/
```
