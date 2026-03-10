# Plan: P1-08A MountTable 核心映射与 RPC

> Status: Done

## Goal

实现 MountTable 的核心职责：挂载、卸载、最长前缀匹配解析，以及 Mount/Unmount/ListMounts RPC 在 MasterService 中的真正接线。

## 依赖

- P1-05A Master 启动骨架（已完成）
- P1-11 UFS LocalFS（已完成）

## Steps

1. **实现 MountTable 核心类**（`src/master/mount_table.h/.cpp`）
   - `Mount(path, ufs_uri)`：挂载逻辑路径到 UFS；重复挂载同一 path 返回稳定错误（InvalidArgument）
   - `Unmount(path)`：卸载挂载点
   - `Resolve(logical_path)`：最长前缀匹配，返回 `(ufs_uri, ufs_path)`；未命中返回 NotFound
   - `ListMounts()`：返回所有挂载路径（有序）
   - 内部用 `std::map<std::string, std::string>` 按 path 字典序存储，便于最长前缀匹配

2. **在 MasterServiceImpl 中接线 RPC**
   - Mount：校验 path、ufs_uri 非空，调用 `mount_table_.Mount()`，将 Status 转为 gRPC Status
   - Unmount：校验 path 非空，调用 `mount_table_.Unmount()`
   - ListMounts：调用 `mount_table_.ListMounts()` 填充 response

3. **实现 mount_table_test.cpp**
   - Mount/Unmount/ListMounts 基本行为
   - Resolve 最长前缀匹配：`/data` → ufs1，`/data/hot` → ufs2，`Resolve("/data/hot/file")` 命中最深挂载点 ufs2
   - 重复 Mount 同一 path 返回稳定错误
   - Resolve 未命中挂载点返回 NotFound（非空字符串、非崩溃）

4. **CMake 集成**
   - master 库添加 mount_table.cpp
   - tests/master 添加 mount_table_test 目标

## Risks & Assumptions

- **Risk**：路径规范化（尾斜杠、重复斜杠）可能影响最长前缀匹配。**Mitigation**：P1-08A 采用简单规范化（去除尾斜杠、合并重复斜杠），根路径 "/" 特殊处理。
- **Assumption**：ufs_uri 格式为 `scheme://authority`（如 `local:///tmp/ufs`），由 UfsFactory 解析，MountTable 仅存储和返回字符串。

## To Confirm

- [x] Resolve 对嵌套挂载使用最长前缀匹配（命中最深挂载点）
- [x] 重复挂载返回 InvalidArgument，消息含 "already mounted"
- [x] 未命中返回 NotFound，消息含 "no mount point"

## 变更分级

P1 — 单服务核心逻辑、公共接口（MountTable）、RPC 行为变化。

## 测试设计

| 场景 | 预期 |
|------|------|
| Mount("/data", "local:///tmp/data") | OK |
| Mount("/data", "local:///other") | InvalidArgument (already mounted) |
| Resolve("/data/file") | ufs_uri=local:///tmp/data, ufs_path=file |
| Mount("/data/hot", "local:///tmp/hot") | OK |
| Resolve("/data/hot/file") | ufs_uri=local:///tmp/hot, ufs_path=file（最长前缀） |
| Resolve("/unknown/path") | NotFound |
| Unmount("/data/hot") | OK |
| Resolve("/data/hot/file") | ufs_uri=local:///tmp/data, ufs_path=hot/file |
| ListMounts() | ["/data", "/data/hot"] 或 ["/data/hot", "/data"]（有序） |
