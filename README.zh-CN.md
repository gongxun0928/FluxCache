# FluxCache

[English](./README.md)

FluxCache 是一个基于 **C++20** 的分布式缓存文件系统原型，参考 Alluxio 和 GooseFS，但当前明确采用“先收敛 MVP，再逐层扩展”的建设方式。

当前项目的第一阶段只围绕这条主线展开：

- 单 `Master`
- 单 `Worker`
- `LocalFS` 作为首个 UFS
- 先只做 `Memory` Page 缓存
- 写路径采用 `write-through`
- Phase 1 使用 `mtime` 做缓存版本校验

## 当前状态

> 当前重点是冻结 Phase 1 契约，并让第一条端到端读写链路具备可稳定验证的实现目标。

因此这个仓库目前代表的是：

- 一个相对稳定的架构方向
- 一套正在快速收敛的实现路线图
- 而不是“README 里出现的能力都已经稳定可用”

## 当前目标架构

```text
Client
  |  gRPC
  v
Single Master (MountTable + InodeTree + WorkerManager + HashRing)
  |  gRPC
  v
Single Worker (Memory Tier + PageStore)
  |
  v
Under File System (LocalFS first)
```

### 组件职责

- `Client`
  - 向 Master 查询文件元数据
  - 本地计算 `BlockId`
  - 将读写请求路由到 Worker
- `Master`
  - 维护命名空间与 inode 元数据
  - 管理挂载点
  - 提供 Worker 列表和 `ring_version`
- `Worker`
  - 提供页级读写
  - 维护本地页缓存
  - 缓存未命中时回源 UFS
- `UFS`
  - 始终是持久化真源

## 已经收敛的设计主线

- `InodeId` 是文件身份。
- `BlockId = (InodeId, BlockIndex)` 是路由身份。
- `PageId = {BlockId, page_index}` 是缓存身份。
- Phase 1 的 Master 不保存 `Block -> Worker` 位置表。
- Phase 1 的 Worker 不负责文件路径元数据。
- Phase 1 使用 `ufs_mtime_ms` 做缓存新鲜度校验。

## Phase 1 读路径

1. Client 调用 `GetFileInfo`。
2. Master 返回 `inode_id`、`size`、`block_size`、`ufs_mtime_ms`、`ring_version`、`workers`、`ufs_uri`、`ufs_path`。
3. Client 本地计算 `BlockId` 和 `page_indices`。
4. Client 向 Worker 发送 `ReadPages`。
5. Worker 先查 `PageStore`。
6. 命中失败或版本过期时，Worker 从 UFS 读取并回填缓存。

## Phase 1 写路径

1. Client 调用 `CreateFile` 或 `GetFileInfo`。
2. Client 本地计算目标 block/page。
3. Client 向 Worker 发送 `WritePages`。
4. Worker 先写 UFS。
5. UFS 写入成功后再更新缓存。
6. Client 调用 `CompleteFile`，让 Master 更新元数据。

## 路线图

权威 roadmap 以 [issues/index.md](./issues/index.md) 为准。

高层阶段为：

- `Phase A`：契约冻结
- `Phase B`：元数据面闭环
- `Phase C`：数据面闭环
- `Phase D`：恢复与删除语义
- `Phase E`：多 Worker 与传输增强
- `Phase F`：多层缓存与淘汰
- `Phase G`：访问入口（`CLI` / `SDK` / `FUSE`）
- `Phase H`：后端扩展（`S3` / `HDFS`）
- `Phase I`：HA 与弹性
- `Phase J`：性能、可观测与质量轨道

### 这些能力仍属于规划中

以下内容不再视为“当前稳定核心”，而是 roadmap 项：

- SSD / HDD 多层缓存
- MetaStore 恢复
- FUSE
- 文件级 MVP 之外的 SDK 能力
- S3 / HDFS 后端
- HA Journal / Raft
- 高级降级与弹性策略
- 生产级可观测性

## 构建与测试

### 依赖

- C++20 编译器
- CMake 3.20+
- Protobuf + gRPC
- RocksDB
- yaml-cpp
- **S3/MinIO**：vcpkg + minio-cpp（必需）

后续阶段的可选依赖：

- FUSE3
- HDFS 客户端库
- Prometheus C++ client

### 构建

S3 UFS（minio-cpp）为必需。推荐使用构建脚本：

```bash
./build.sh
```

脚本会自动使用项目内 `vcpkg/`（或环境变量 `VCPKG_ROOT`），必要时执行 bootstrap，并完成编译。可选：`./build.sh [构建目录] [cmake参数...]`，例如 `./build.sh build -DFLUXCACHE_ENABLE_FUSE=ON`。

或手动使用 vcpkg：

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg路径]/scripts/buildsystems/vcpkg.cmake
cmake --build .
# IDE 智能提示：ln -sf "$(pwd)/compile_commands.json" ../compile_commands.json
```

**IDE 智能提示（clangd / C++ 扩展）：** 先执行一次 `./build.sh`（或手动 cmake 后创建上述软链接）。这样 Cursor/VSCode 才能正确解析 `worker/page/page_store.h` 等 include 并跳转到标准库头文件。

### 测试

```bash
cd build
ctest --output-on-failure
```

## 贡献约束

如果修改 roadmap 或实现边界，请同时保持以下文件一致：

- `README.md`
- `README.zh-CN.md`
- `issues/index.md`
- `plan/issue-status.md`
- `design/` 下相关设计文档

## License

[MIT License](./LICENSE)
