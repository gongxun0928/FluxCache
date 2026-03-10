# P1-08A: MountTable 核心映射与 RPC

## 阶段与优先级

Phase B — 元数据面闭环 | P1

## 依赖

- [P1-05A](./P1-05a-master-server-bootstrap.md)
- [P1-11](./P1-11-ufs-localfs.md)

## 描述

实现 `MountTable` 的核心职责：挂载、卸载、最长前缀匹配解析，以及 `Mount/Unmount/ListMounts` RPC。

本 issue 不处理 UFS 懒同步和卸载安全性，这部分由 `P1-08B` 单独处理。

## 交付物

- `src/master/mount_table.h/.cpp`
- `Mount`
- `Unmount`
- `Resolve`
- `ListMounts`
- `MasterService` 对应 RPC 真正接线

## 验收标准

- [ ] 可通过 RPC 创建和查询挂载点。
- [ ] `Resolve` 对嵌套挂载使用最长前缀匹配。
- [ ] 重复挂载同一逻辑路径返回稳定错误。
- [ ] 未命中挂载点时返回明确错误，而非空字符串或崩溃。
- [ ] 单元测试覆盖 `Resolve("/data/hot/file")` 命中最深挂载点的场景。

## 涉及目录

```text
src/master/
tests/master/
```
