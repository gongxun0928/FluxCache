# P1-11: UFS 抽象层与 LocalFS 驱动

## 阶段与优先级

Phase A — 契约冻结 | P1

## 依赖

- [P1-01](./P1-01-project-scaffolding.md)

## 描述

实现 Under File System 的最小抽象层，并提供 `LocalFS` 作为 Phase 1 唯一必须落地的后端。

为避免后续缓存一致性返工，本 issue 需要把 `FileStatus` 的最小字段一次说清楚，而不是留给后续 issue 自行脑补。

## 交付物

- `src/ufs/ufs.h`
- `src/ufs/ufs_factory.h/.cpp`
- `src/ufs/local_ufs.h/.cpp`

最小接口：

- `Read(path, offset, size)`
- `Write(path, offset, data)`
- `GetStatus(path)`
- `List(path)`
- `Delete(path)`
- `Rename(src, dst)`
- `Mkdirs(path)`

`FileStatus` 至少包含：

- `exists`
- `is_directory`
- `size`
- `mtime_ms`
- `path`

## 验收标准

- [ ] LocalFS 可在临时目录下完成创建、读取、写入、删除。
- [ ] `GetStatus` 返回稳定的 `size` 与 `mtime_ms`。
- [ ] `List` 返回目录下子项及其基本状态。
- [ ] 路径不存在时返回明确错误，不崩溃。
- [ ] 测试不依赖宿主文件系统时间抖动；必要时可通过写后再次 `GetStatus` 显式获取新 `mtime_ms`。

## 涉及目录

```text
src/ufs/
tests/ufs/
```
