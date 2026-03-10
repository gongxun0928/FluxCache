# 配置加载系统设计

> 日期: 2026-03-11
> 状态: Phase 1（MVP 主线）
> 关联: [P1-02](../issues/P1-02-config-system.md)

## 1. 概述

实现统一配置加载框架，供 Master、Worker、Client 共享使用。Phase 1 重点是为 MVP 主线提供稳定配置契约，不提前要求 S3/HDFS/FUSE/HA 专属字段。

## 2. 设计决策

### 2.1 依赖选择

- **yaml-cpp**：通过 FetchContent 引入，与 GTest 一致，保证版本可复现。
- 单文件 YAML 根节点为 `fluxcache`，下挂 `master`、`worker`、`client`、`ufs` 四个子节点。

### 2.2 配置结构（Phase 1 MVP）

| 结构体 | 必填字段 | 可选字段 | 说明 |
|--------|----------|----------|------|
| MasterConfig | host, port | - | Master 监听地址 |
| WorkerConfig | data_dir | - | Worker 数据目录 |
| ClientConfig | master_host, master_port | - | Client 连接 Master 的地址 |
| UfsConfig | type, path | - | UFS 类型与路径；Phase 1 仅支持 `localfs` |

### 2.3 错误处理

- 缺失必填字段：返回 `Status::InvalidArgument`，错误消息包含字段名。
- 类型错误（如 port 非整数）：同上。
- 文件不存在或无法解析：返回 `Status::IOError` 或 `Status::InvalidArgument`。

### 2.4 非目标（Phase 1）

- S3/HDFS/FUSE/HA 专属配置字段。
- 环境变量覆盖、多文件合并、热加载。

## 3. YAML 模板结构

```yaml
fluxcache:
  master:
    host: "127.0.0.1"
    port: 9090
  worker:
    data_dir: "/tmp/fluxcache_worker"
  client:
    master_host: "127.0.0.1"
    master_port: 9090
  ufs:
    type: "localfs"
    path: "/tmp/fluxcache_ufs"
```

## 4. 测试设计

| 场景 | 输入 | 预期 |
|------|------|------|
| 正常加载 | 完整有效 YAML | 所有字段正确解析 |
| 缺字段 | 缺少 master.host | Status::InvalidArgument，消息含 "master.host" |
| 类型错误 | port 为字符串 "abc" | Status::InvalidArgument，消息含类型信息 |
| 文件不存在 | 不存在的路径 | Status::IOError 或 InvalidArgument |
| 空/无效 YAML | 空文件或非法 YAML | Status::InvalidArgument |

## 5. 接口设计

```cpp
// config/config.h
namespace fluxcache {

struct MasterConfig {
  std::string host;
  uint16_t port;
};

struct WorkerConfig {
  std::string data_dir;
};

struct ClientConfig {
  std::string master_host;
  uint16_t master_port;
};

struct UfsConfig {
  std::string type;   // "localfs" for Phase 1
  std::string path;
};

struct FluxCacheConfig {
  MasterConfig master;
  WorkerConfig worker;
  ClientConfig client;
  UfsConfig ufs;
};

// 从文件加载完整配置
StatusOr<FluxCacheConfig> LoadConfig(const std::string& path);

// 或分别加载（按需）
StatusOr<MasterConfig> LoadMasterConfig(const std::string& path);
// ...
}  // namespace fluxcache
```

`StatusOr<T>` 可复用或仿照 `absl::StatusOr` 语义：成功时持有 T，失败时持有 Status。

## 6. 风险与假设

- **风险**：yaml-cpp API 与项目 C++20 兼容性。**缓解**：使用稳定 tag，本地验证。
- **假设**：Phase 1 仅需单文件加载，无需多环境、多 profile。
