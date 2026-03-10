# Todo: P1-08B SyncFromUfs 与 Unmount 安全规则

> Status: Done

## Items

### 1. [x] InodeTree.HasInodesUnderPath

- Files: `src/master/inode_tree.h`, `src/master/inode_tree.cpp`
- DoD:
  - [ ] `HasInodesUnderPath(prefix)` 返回 true 当且仅当存在 inode 其逻辑路径以 prefix 为前缀
  - [ ] 根路径 "/" 或 prefix 为空时按约定处理

### 2. [x] UFS URI 解析与 PathResolver

- Files: `src/master/path_resolver.h`, `src/master/path_resolver.cpp`
- DoD:
  - [ ] 解析 `local:///path` 为 scheme + authority
  - [ ] `SyncFromUfs(logical_path)`：Resolve → UFS.List → 递归补齐父目录 → 创建缺失 inode
  - [ ] 与 InodeTree、MountTable 集成

### 3. [x] MasterServer 传入 InodeTree 给 MasterServiceImpl

- Files: `src/master/master_server.h`, `src/master/master_server.cpp`, `src/master/master_service_impl.h`
- DoD:
  - [ ] MasterServiceImpl 持有 InodeTree*（或 shared）
  - [ ] MasterServer 构造时传入 inode_tree_.get()

### 4. [x] Unmount 安全检查

- Files: `src/master/master_service_impl.cpp`
- DoD:
  - [ ] Unmount 前调用 InodeTree.HasInodesUnderPath(mount_path)
  - [ ] 若 true 返回 InvalidArgument，消息含 "active inodes"

### 5. [x] GetFileInfo 按需补齐

- Files: `src/master/master_service_impl.cpp`, `src/master/path_resolver.cpp`
- DoD:
  - [ ] LookupPath 失败时，若 MountTable.Resolve 命中，则 SyncFromUfs(parent) + SyncFromUfs(path)
  - [ ] 返回最小 FileInfo（inode_id, size, block_size, mtime, ufs_uri, ufs_path）

### 6. [x] ListDirectory 按需补齐（内部 API）

- Files: `src/master/path_resolver.h`, `src/master/path_resolver.cpp`
- DoD:
  - [ ] `ListDirectory(path)`：SyncFromUfs(path) → LookupPath → InodeTree.ListDirectory
  - [ ] 可被 GetFileInfo 或后续 RPC 复用

### 7. [x] sync_from_ufs_test.cpp

- Files: `tests/master/sync_from_ufs_test.cpp`
- DoD:
  - [ ] 临时目录作 UFS，Mount、SyncFromUfs
  - [ ] 补齐前 LookupPath 为 nullopt，补齐后可解析
  - [ ] ListDirectory 返回补齐后目录项
  - [ ] Unmount 有活跃 inode 时返回明确错误

### 8. [x] CMake 集成

- Files: `src/master/CMakeLists.txt`, `tests/master/CMakeLists.txt`
- DoD:
  - [ ] 添加 path_resolver.cpp 到 fluxcache_master
  - [ ] 添加 sync_from_ufs_test 目标，链接 fluxcache_ufs
