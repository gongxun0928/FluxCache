# Plan: P1-08B SyncFromUfs 与 Unmount 安全规则

> Status: Done

## Goal

补齐 MountTable 与 InodeTree 之间的真实边界：当路径命中挂载点但 inode 不存在时，允许通过 UFS 元数据补齐；同时定义卸载前的安全检查。

## 依赖

- P1-05B InodeStore 与 InodeTree（已完成）
- P1-08A MountTable 核心映射（已完成）
- P1-11 UFS LocalFS（已完成）

## Steps

1. **引入 PathResolver / SyncFromUfs 协调层**
   - 新建 `src/master/path_resolver.h/.cpp`，持有 InodeTree*、MountTable*、UfsFactory 能力
   - `SyncFromUfs(logical_path)`：Resolve path → UFS.List(ufs_path) → 对每个 UFS 条目，若 InodeTree 无对应 inode 则创建（需递归补齐父目录）
   - 解析 ufs_uri（如 `local:///tmp/data`）为 scheme + authority，调用 CreateUFS

2. **InodeTree 扩展**
   - `EnsurePath(logical_path, ufs, ufs_path)`：若 path 不存在，从 UFS 补齐父目录链，再补齐目标；返回 inode_id 或 nullopt
   - 或：InodeTree 保持纯存储，由 PathResolver 调用 CreateDirectory/CreateFile + LookupPath 完成补齐
   - 新增 `HasInodesUnderPath(prefix)`：判断是否存在路径以 prefix 为前缀的 inode（用于 Unmount 检查）

3. **MountTable.Unmount 安全检查**
   - 调用 InodeTree.HasInodesUnderPath(mount_path)，若 true 则返回 InvalidArgument("Unmount: mount point has active inodes or discovered children")
   - MountTable 需持有 InodeTree* 或通过回调注入检查逻辑；为减少耦合，Unmount 接受 `std::function<bool(const std::string&)> has_inodes_under` 或由 MasterServiceImpl 在调用 Unmount 前先检查

4. **MasterServiceImpl 集成**
   - MasterServer 将 InodeTree* 传入 MasterServiceImpl
   - MasterServiceImpl 持有 PathResolver（或内联 SyncFromUfs 逻辑）
   - GetFileInfo：LookupPath 失败时，若 Resolve 命中挂载点，则 SyncFromUfs(parent) + SyncFromUfs(path)，再 LookupPath
   - ListDirectory：新增内部 API 或通过 GetFileInfo 间接；ListDirectory(path) = SyncFromUfs(path) + LookupPath(path) + InodeTree.ListDirectory(dir_id)
   - Unmount：先调用 InodeTree.HasInodesUnderPath(path)，若 true 则返回错误；否则调用 MountTable.Unmount

5. **测试**
   - `tests/master/sync_from_ufs_test.cpp`：使用临时目录作为假 UFS，Mount、SyncFromUfs、验证补齐前后 LookupPath/ListDirectory 行为
   - Unmount 拒绝：Mount、CreateFile 或 SyncFromUfs 产生 inode 后，Unmount 应返回明确错误

## Risks & Assumptions

- **Risk**：ufs_uri 解析（`local:///tmp/data`）需与 UfsFactory 约定一致。**Mitigation**：采用 `scheme://authority` 解析，`//` 后取 path 作为 authority。
- **Assumption**：Phase 1 仅支持 local UFS，CreateUFS 已支持。
- **Assumption**："已发现的子项" 在 Phase 1 简化为：凡 SyncFromUfs 写入的即持久化，无内存 pending 态；HasInodesUnderPath 覆盖"活跃 inode"检查。

## To Confirm

- [x] SyncFromUfs 递归补齐父目录（UFS 有 /a/b/file，需先建 /a、/a/b）
- [x] Unmount 前检查 HasInodesUnderPath，有则拒绝
- [x] GetFileInfo/ListDirectory 触发按需补齐

## 变更分级

P1 — 单服务核心逻辑、MountTable/InodeTree 接口扩展、RPC 行为变化。

## 测试设计

| 场景 | 预期 |
|------|------|
| Mount + SyncFromUfs("/data")，UFS 有 file | LookupPath("/data/file") 返回 inode_id |
| 补齐前 LookupPath("/data/file") | nullopt |
| 补齐后 ListDirectory(dir_id for /data) | 含 file |
| Mount + SyncFromUfs 产生 inode 后 Unmount("/data") | InvalidArgument |
| 无 inode 时 Unmount("/data") | OK |
