# Plan: P1-15A Master.CreateFile / CompleteFile

> Status: Done

## Goal

实现写路径所需的两个元数据 RPC：`CreateFile` 和 `CompleteFile`。CreateFile 负责分配 `inode_id`、建立最小文件元数据；CompleteFile 在 write-through 成功后更新 `size` 与 `mtime`。

## 现状分析

- **Proto**：CreateFileRequest(path)、CreateFileResponse(file_info, ufs_uri, ufs_path)、CompleteFileRequest(inode_id, size)、CompleteFileResponse 已定义。CompleteFileRequest 缺少 `ufs_mtime_ms` 字段。
- **MasterServiceImpl**：CreateFile、CompleteFile 当前返回 UNIMPLEMENTED。
- **InodeTree**：已有 `CreateFile(path)` 与 `CreateFile(path, size, block_size, mtime_ms)`，无 `UpdateInodeSizeAndMtime`。
- **InodeStore**：有 PutInode/GetInode，可通过 Get+Put 实现更新。

## 测试设计

| 场景 | 输入 | 预期 | 验证方式 |
|------|------|------|----------|
| CreateFile 成功 | Mount + 合法路径（不存在） | 返回 inode_id、file_info、ufs_uri、ufs_path | 断言各字段非空 |
| CreateFile 重复创建 | 路径已存在 | gRPC ALREADY_EXISTS | 断言 error_code |
| CreateFile 非法路径 | 空路径 / 非挂载路径 | gRPC INVALID_ARGUMENT / NOT_FOUND | 断言 error_code |
| CompleteFile 成功 | 有效 inode_id + size + mtime | OK | 断言 status.ok() |
| GetFileInfo 读更新后属性 | CompleteFile 后 GetFileInfo | size、ufs_mtime_ms 为更新值 | 断言 FileInfo 字段 |

## Steps

1. **Proto 扩展**：CompleteFileRequest 新增 `int64 ufs_mtime_ms = 3`。
2. **InodeTree**：新增 `UpdateInodeSizeAndMtime(inode_id, size, mtime_ms)`，内部 GetInode + 修改 + PutInode。
3. **MasterServiceImpl CreateFile**：
   - 校验 path 非空、绝对；
   - MountTable::Resolve(path) 失败则 NOT_FOUND（非法路径：非挂载下）；
   - SyncFromUfs(Dirname(path)) 确保父目录存在；
   - LookupPath(path) 已存在则 ALREADY_EXISTS；
   - InodeTree::CreateFile(path) 创建，返回 file_info、ufs_uri、ufs_path。
4. **MasterServiceImpl CompleteFile**：
   - 校验 inode_id > 0；
   - GetInode 不存在或为目录则 NOT_FOUND / INVALID_ARGUMENT；
   - UpdateInodeSizeAndMtime(inode_id, size, ufs_mtime_ms)。
5. **create_complete_file_test.cpp**：覆盖上述场景。

## Risks & Assumptions

- **Risk**：CreateFile 路径下父目录可能尚未通过 SyncFromUfs 创建。**Mitigation**：先 SyncFromUfs(Dirname(path))，若 Resolve 失败则路径非法。
- **Assumption**：CompleteFile 的 mtime 由 Client 在 write-through 后从 UFS 获取并传入；未传时使用 0 或保持原值（本 Plan 采用：未传则保持原 modification_time_ms）。

## Files

| 文件 | 变更 |
|------|------|
| `src/proto/master.proto` | CompleteFileRequest 新增 ufs_mtime_ms |
| `src/master/inode_tree.h` | 新增 UpdateInodeSizeAndMtime 声明 |
| `src/master/inode_tree.cpp` | 实现 UpdateInodeSizeAndMtime |
| `src/master/master_service_impl.cpp` | 实现 CreateFile、CompleteFile |
| `tests/master/create_complete_file_test.cpp` | 新建测试 |
| `tests/master/CMakeLists.txt` | 注册 create_complete_file_test |

## 变更分级

P1 — 影响 Master 核心 RPC、InodeTree 接口、Proto 契约。

## 验收标准（对齐 Issue）

- [x] CreateFile 返回稳定的 inode_id 与必要路径信息
- [x] CompleteFile 可更新 size 与 mtime
- [x] 再次调用 GetFileInfo 时可读到更新后的属性
- [x] 针对重复创建和非法路径返回明确错误
