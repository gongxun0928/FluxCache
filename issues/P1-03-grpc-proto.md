# P1-03: Phase 1 最小 RPC 契约

## 阶段与优先级

Phase A — 契约冻结 | P1

## 依赖

- [P1-01](./P1-01-project-scaffolding.md)
- [P1-16](./P1-16-core-types.md)
- [P1-11](./P1-11-ufs-localfs.md)

## 描述

定义只服务于 Phase 1 MVP 的最小 RPC 契约。

本 issue 的目标不是一次性冻结“最终完整协议”，而是为 `单 Master + 单 Worker + LocalFS + write-through + mtime 校验` 主线提供稳定合同。目录类变更、rename、批量 RPC、HA 复制字段等高级能力不在本 issue 中承诺。

## 交付物

- `src/proto/common.proto`
- `src/proto/master.proto`
- `src/proto/worker.proto`
- `fluxcache_proto` 静态库

最小消息集合应覆盖：

- `WorkerEndpoint`
- `FileInfo`
- `GetFileInfo`
- `CreateFile`
- `CompleteFile`
- `DeleteFile`
- `Mount` / `Unmount` / `ListMounts`
- `RegisterWorker`
- `GetHashRing`
- `ReadPages`
- `WritePages`
- `Heartbeat`

## 契约要求

- `FileInfo` 至少包含：
  - `inode_id`
  - `size`
  - `block_size`
  - `ufs_mtime_ms`
  - `is_directory`
- `GetFileInfoResponse` 至少包含：
  - `file_info`
  - `ring_version`
  - `workers`
  - `ufs_uri`
  - `ufs_path`
- `ReadPagesRequest` 至少包含：
  - `block_id`
  - `page_indices`
  - `expected_mtime_ms`
  - `ufs_uri`
  - `ufs_path`
- `WritePagesRequest` 至少包含：
  - `block_id`
  - `page_indices`
  - `data`
  - `ufs_uri`
  - `ufs_path`
- `Heartbeat` 可预留 GC 对账字段，但 Phase 1 不要求实现完整逻辑。

## 验收标准

- [ ] `cmake --build .` 可生成全部 proto C++ 代码。
- [ ] `fluxcache_proto` 可被 Master、Worker、Client 模块链接。
- [ ] Phase 1 读写主路径所需字段全部齐备，无额外依赖隐藏在文档外。
- [ ] 不提前承诺 `Rename`、目录 CRUD、批量读、流式 pipeline、HA 日志复制字段。
- [ ] 通过一个小型契约测试验证 proto 编解码与字段可达性。

## 涉及目录

```text
src/proto/
tests/proto/
```
