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

后续阶段的可选依赖：

- FUSE3
- AWS SDK 或其他 S3 客户端栈
- HDFS 客户端库
- Prometheus C++ client

### 构建

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

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
# FluxCache

[English](./README.md)

FluxCache 是一个基于 **C++20** 的分布式缓存文件系统，参考 [Alluxio](https://www.alluxio.io/)，提供 **统一命名空间**、**多级缓存加速** 与 **全链路 gRPC 通信**能力。

> 状态：当前处于架构与接口持续构建阶段。

## 读者分层与导航

### 面向普通用户（运维 / 数据平台使用方）

建议优先阅读：

- [核心特性](#核心特性)
- [RPC 与超时模型](#rpc-与超时模型)
- [读路径（简化）](#读路径简化)
- [写路径（写穿模式）](#写路径写穿模式)
- [配置（规划中）](#配置规划中)

用户侧关注点：

- 混合存储上的统一命名空间；
- 热数据自动缓存加速；
- 通过 FUSE/SDK/CLI 的一致访问体验。

### 面向开发者（贡献者 / 集成开发）

建议优先阅读：

- [架构](#架构)
- [构建与测试（开发者指南）](#构建与测试开发者指南)
- [路线图（高层）](#路线图高层)
- [设计目标与非目标](#设计目标与非目标)
- [贡献](#贡献)

## 项目目标

在混合存储环境（本地磁盘、对象存储、分布式文件系统）下，为上层应用提供：

- 一个统一的逻辑路径空间；
- 面向冷热分层的多级缓存能力（Memory -> SSD -> HDD）；
- POSIX 兼容访问与 SDK/CLI 控制能力；
- 可观测、可恢复、可扩展的分布式缓存系统骨架。

## 架构

```text
Client (FUSE / SDK / CLI)
        |  gRPC
        v
Master Cluster (Metadata + Namespace)
        |  gRPC
        v
Worker Pool (Multi-tier Cache: Memory -> SSD -> HDD)
        |
        v
Under File System (Local FS / S3 / HDFS)
```

### 组件职责

- **Client**
  - 提供 FUSE 挂载（POSIX 访问）；
  - 提供 SDK / CLI（程序化与运维访问）；
  - 使用 gRPC `ChannelPool` 复用 HTTP/2 连接，降低建连开销。
- **Master Cluster**
  - 维护全局元数据与命名空间；
  - 管理挂载表（MountTable）；
  - 提供基于 Raft 的 Journal 复制骨架（HA 准备）。
- **Worker Pool**
  - 承担数据面读写和缓存服务；
  - 执行分层放置、晋升与淘汰；
  - 持久化页级索引，支持快速重启恢复。
- **Under File System (UFS)**
  - 提供可插拔底层存储抽象（Local FS / S3 / HDFS）；
  - 作为持久化数据源。

## 核心特性

- **统一命名空间 (`MountTable`)**
  - 多个底层存储挂载到同一逻辑路径树。
- **多级缓存 (`StorageTier`)**
  - Memory / SSD / HDD 分层，自动晋升与淘汰。
- **页级缓存 (`PageStore`)**
  - 1MB 粒度缓存，优化随机小 I/O。
- **元数据持久化 (`MetaStore`)**
  - 基于 RocksDB，Worker 重启后快速恢复缓存索引。
- **可插拔淘汰策略**
  - 支持 LRU / LFU。
- **全链路 gRPC**
  - 组件通信统一为 RPC，支持连接池复用与超时配置。
- **FUSE 挂载**
  - POSIX 兼容，可直接使用标准文件工具。
- **S3 后端**
  - 兼容 AWS S3 协议的对象存储。
- **HDFS 后端（规划中）**
  - 通过 HDFS/WebHDFS 对接 Hadoop 兼容分布式文件系统。
- **自动缓存层级管理**
  - 热数据自动晋升，容量不足自动淘汰。
- **Master HA 骨架**
  - 基于 Raft 的 Journal 复制框架。
- **监控指标**
  - Prometheus 兼容的 `/metrics` 端点。

## RPC 与超时模型

所有组件间统一通过 gRPC 通信。  
每个 RPC 都应配置明确的 deadline/timeout，避免慢后端或网络抖动导致 I/O 线程阻塞。

原则：

- 优先连接复用（`ChannelPool`）；
- 单次调用具备有界时延；
- 在语义安全前提下进行快速失败与重试。

## 读路径（简化）

1. Client 通过 gRPC 向 Master 查询文件元数据与 Worker 位置。
2. Client 直连目标 Worker 发起数据读请求。
3. Worker 按层级顺序检查 `PageStore`（Memory -> SSD -> HDD）。
4. 缓存未命中时，从 UFS 回源读取。
5. 数据按页切分返回，并写入缓存层。
6. 热页晋升，冷页按策略淘汰。

## 写路径（写穿模式）

1. Client 向 Master 查询元数据后，向目标 Worker 发起写请求。
2. Worker 同步写入 UFS（write-through）。
3. Worker 更新缓存页与索引元数据。
4. Master 更新命名空间与映射信息。

## 构建与测试（开发者指南）

> 当前仓库处于早期引导阶段，以下流程作为推荐基线。

### 依赖

- C++20 编译器：
  - GCC 11+ 或 Clang 14+（推荐）；
- CMake 3.20+；
- Protobuf + gRPC；
- RocksDB；
- FUSE3 开发头文件（用于 FUSE 客户端）；
- OpenSSL（通常为 gRPC/S3 依赖）；
- Prometheus C++ 客户端库（或等价指标导出组件）。

### 构建步骤

```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j
```

### 测试（推荐质量门）

```bash
cd build
ctest --output-on-failure
```

## 配置（规划中）

- `master.*`：元数据服务地址、选主与 Journal 参数；
- `worker.*`：缓存层容量、页大小、淘汰策略；
- `client.*`：RPC 超时、重试预算、连接池大小；
- `ufs.*`：后端类型与访问凭据（S3/HDFS/local）。

## 路线图（高层）

- [ ] 命名空间与挂载表生命周期接口；
- [ ] Worker 页缓存引擎与分层放置策略；
- [ ] 端到端读写协议与错误模型；
- [ ] CLI 与 SDK 客户端接口；
- [ ] Master HA Journal 复制能力完善；
- [ ] HDFS 后端驱动；
- [ ] 生产级可观测与性能剖析。

## 设计目标与非目标

### 目标

- 混合负载下可预测时延；
- 元数据面与数据面清晰解耦；
- 通过统一命名空间屏蔽后端差异。

### 非目标（当前阶段）

- 替代长期持久化存储；
- 提供文件系统语义之外的应用级一致性机制。

## 贡献

欢迎贡献。提交变更时建议包含：

- 变更动机与预期行为；
- 架构影响说明（Master/Worker/Client/UFS）；
- 测试计划与风险说明。

## 许可证

本项目采用 [MIT License](./LICENSE)。

