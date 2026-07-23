# FluxCache

[English](./README.md)

FluxCache 是一个基于 **C++20** 的分布式缓存文件系统原型，参考 Alluxio 和 GooseFS，但当前明确采用“先收敛 MVP，再逐层扩展”的建设方式。

项目已从窄 MVP 扩展到覆盖 Phase A–K 的大部分能力：

- 单 `Master`（RocksDB 持久化元数据；HA Raft 已暂停）
- 一个或多个 `Worker`，支持 Memory / SSD / HDD 分层
- UFS：`LocalFS`（首个）、`S3/MinIO`（必需）、`HDFS`（stub / 可选）
- Page 缓存默认 `write-through`，并具备 write-back 淘汰等扩展路径
- 缓存新鲜度由 Master 维护的 `file_version` 校验（已替代早期 `mtime` 方案）

## 当前状态

> 当前重点：收尾 Phase K 残余（pjdfstest 实测基线），再选择下一方向——加固现有架构，或冻结更大范围的重规划（见未合入的 draft PR #1）。

状态真相源：[plan/issue-status.md](./plan/issue-status.md)。活跃批次：[plan/active-batch.md](./plan/active-batch.md)。

因此这个仓库目前代表的是：

- 相对稳定的架构方向，且 Phase A–J / Phase K 大部分已实现
- 不是“已生产加固”的声明（HA 暂停；POSIX 属性落盘 / 完整 truncate 仍有缺口）

## 当前目标架构

```text
Client (CLI / SDK / 可选 FUSE)
  |  gRPC
  v
Single Master (MountTable + InodeTree + WorkerManager + HashRing)
  |  gRPC
  v
Worker(s) (Memory/SSD/HDD Tier + PageStore + MetaStore)
  |
  v
Under File System (LocalFS / S3 / HDFS stub)
```

### 组件职责

- `Client`
  - 向 Master 查询文件元数据与命名空间操作
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
- Master 不保存 `Block -> Worker` 位置表；Client 通过 HashRing 路由。
- Worker 不负责文件路径元数据。
- 缓存新鲜度使用 Master 的 `file_version`（在 CompleteFile / 写路径上递增）。

## 读路径

1. Client 调用 `GetFileInfo`。
2. Master 返回 `inode_id`、`size`、`block_size`、`file_version`、`ring_version`、`workers`、`ufs_uri`、`ufs_path`。
3. Client 本地计算 `BlockId` 和 `page_indices`。
4. Client 向 Worker 发送带 `expected_file_version` 的 `ReadPages`。
5. Worker 先查 `PageStore`。
6. 未命中或版本不匹配时，Worker 从 UFS 读取并回填缓存。

## 写路径

1. Client 调用 `CreateFile` 或 `GetFileInfo`。
2. Client 本地计算目标 block/page。
3. Client 向 Worker 发送 `WritePages`。
4. Worker 先写 UFS（默认 write-through）。
5. UFS 写入成功后再更新缓存。
6. Client 调用 `CompleteFile`，Master 更新 size 并递增 `file_version`。

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
- `Phase K`：生产就绪增强（命名空间 RPC、Docker e2e、pjdfstest）

### 仍开放或能力受限的项

- **P5-05** pjdfstest：脚本与预期基线文档已有；缺实测报告与 Compose 集成
- FUSE 属性落盘 / 完整 truncate（已知 POSIX 缺口）
- HDFS UFS 真实驱动（无 libhdfs 时为 stub）
- Master HA / Raft（`P4-02` 已暂停；默认 `FLUXCACHE_ENABLE_RAFT=OFF`）
- 五层架构重规划草案（open draft PR #1，未合入）

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
