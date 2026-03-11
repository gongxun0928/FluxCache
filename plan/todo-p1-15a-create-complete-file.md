# Todo: P1-15A Master.CreateFile / CompleteFile

> 对应 Plan: [plan_p1-15a-create-complete-file.md](plan_p1-15a-create-complete-file.md)

## DoD（完成定义）

- CreateFile 返回 inode_id、file_info、ufs_uri、ufs_path
- CompleteFile 更新 InodeTree 中 inode 的 size、mtime
- GetFileInfo 可读到 CompleteFile 后的属性
- 重复创建返回 ALREADY_EXISTS，非法路径返回 INVALID_ARGUMENT / NOT_FOUND
- create_complete_file_test 全部通过

## 任务列表

### 1. [x] Proto 扩展

- 文件：`src/proto/master.proto`
- DoD：CompleteFileRequest 新增 `optional int64 ufs_mtime_ms = 3`
- 验证：重新生成 pb 后编译通过

### 2. [x] InodeTree::UpdateInodeSizeAndMtime

- 文件：`src/master/inode_tree.h`、`src/master/inode_tree.cpp`
- DoD：`bool UpdateInodeSizeAndMtime(InodeId id, uint64_t size, int64_t mtime_ms)` 实现
- 验证：create_complete_file_test 覆盖

### 3. [x] MasterServiceImpl::CreateFile

- 文件：`src/master/master_service_impl.cpp`
- DoD：路径校验、Resolve、SyncFromUfs、LookupPath 判重、CreateFile、填充响应
- 验证：create_complete_file_test

### 4. [x] MasterServiceImpl::CompleteFile

- 文件：`src/master/master_service_impl.cpp`
- DoD：inode_id 校验、GetInode、UpdateInodeSizeAndMtime
- 验证：create_complete_file_test

### 5. [x] create_complete_file_test.cpp

- 文件：`tests/master/create_complete_file_test.cpp`
- DoD：CreateFile 成功、重复创建、非法路径；CompleteFile 成功；GetFileInfo 读更新后属性
- 验证：`ctest -R create_complete --output-on-failure`

### 6. [x] CMakeLists 注册

- 文件：`tests/master/CMakeLists.txt`
- DoD：add_executable + add_test 注册 create_complete_file_test
