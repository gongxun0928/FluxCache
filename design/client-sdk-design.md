# Client SDK 与本地缓存设计

> 日期: 2026-03-10
> 状态: Phase 2 设计

## 1. 概述

FluxCache 提供 C++ Client SDK，供外部服务集成以读写缓存数据。SDK 对外暴露文件级 CRUD API，内部通过 Block/Page 两层模型和 hash ring 路由与 FluxCache 集群通信，对用户完全透明。

为减少网络往返，SDK 内置可选的 Client 端 Page 内存缓存（L1 缓存），与 Worker 端 PageStore（L2 缓存）和 UFS（L3 存储）形成三级缓存层次。

## 1.1 范围说明

本设计文档包含两个层次：

- **SDK MVP（当前 roadmap 覆盖）**：`Create / Open / Read / Write / Stat / Delete`
- **后续 namespace 扩展（暂不纳入 SDK MVP issue 验收）**：`Rename / List / Mkdir / Rmdir / Exists`

原因是当前服务端稳定契约首先围绕文件级主路径收敛，目录类和 rename 语义需要在后续单独建模与落地。

```
┌──────────────────────────────────────────────────────────────┐
│                     User Application                          │
├──────────────────────────────────────────────────────────────┤
│                     FluxCache C++ SDK                         │
│                                                              │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  Public API (FluxCacheSDK / FileHandle)                  │ │
│  │  Open / Read / Write / Close / Create / Delete           │ │
│  │  Rename / Stat / List / Exists / Mkdir / Rmdir           │ │
│  ├─────────────────────────────────────────────────────────┤ │
│  │  ClientPageCache [L1]                                    │ │
│  │  内存 LRU · mtime 失效 · 可配置大小 · 可禁用             │ │
│  ├─────────────────────────────────────────────────────────┤ │
│  │  Transport Layer                                         │ │
│  │  CachedHashRing · MasterClient · WorkerClient            │ │
│  │  ChannelPool · 重试 · 超时                                │ │
│  └─────────────────────────────────────────────────────────┘ │
├──────────────────────────────────────────────────────────────┤
│                     FluxCache Cluster                         │
│  Master ─── Worker [L2: PageStore] ─── UFS [L3]              │
└──────────────────────────────────────────────────────────────┘
```

**三级缓存层次**：

| 层级 | 位置 | 介质 | 延迟 | 生命周期 |
|---|---|---|---|---|
| L1 | Client 进程内 | 内存 | 微秒 | 进程重启即失效 |
| L2 | Worker | 内存/SSD/HDD | 毫秒 + 网络 | Worker 重启可恢复（MetaStore） |
| L3 | UFS | 磁盘/S3/HDFS | 高 | 持久化 |

---

## 2. SDK 公开 API

仅暴露文件级操作，Block/Page 概念对用户完全不可见。

### 2.1 FluxCacheSDK

```cpp
class FluxCacheSDK {
public:
    static std::unique_ptr<FluxCacheSDK> Create(const SDKConfig& config);
    void Shutdown();

    // 文件操作
    std::unique_ptr<FileHandle> Open(const std::string& path, OpenMode mode);
    Status Create(const std::string& path, const CreateOptions& opts = {});
    Status Delete(const std::string& path);
    Status Rename(const std::string& src, const std::string& dst);

    // 元数据查询
    StatusOr<FileInfo> Stat(const std::string& path);
    StatusOr<std::vector<FileInfo>> List(const std::string& path);
    bool Exists(const std::string& path);

    // 目录操作
    Status Mkdir(const std::string& path, bool recursive = false);
    Status Rmdir(const std::string& path, bool recursive = false);

private:
    struct Impl;                    // PIMPL 隔离内部依赖
    std::unique_ptr<Impl> impl_;
};
```

### 2.2 FileHandle

```cpp
class FileHandle {
public:
    StatusOr<size_t> Read(void* buf, size_t offset, size_t size);
    StatusOr<size_t> Write(const void* buf, size_t offset, size_t size);
    Status Flush();
    Status Close();
    StatusOr<FileInfo> Stat();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

`FileHandle::Impl` 内部持有：

```cpp
struct FileHandle::Impl {
    InodeId inode_id;
    uint64_t file_size;
    size_t block_size;
    int64_t ufs_mtime_ms;          // Open 时获取的 mtime 快照
    std::string ufs_uri;           // MountTable 解析的 UFS URI
    std::string ufs_path;          // MountTable 解析的 UFS 相对路径
    OpenMode mode;
    FluxCacheClient* client;        // 指向内部 Client 实例
    ClientPageCache* local_cache;   // 指向共享的本地缓存（可为 nullptr）
};
```

### 2.3 设计要点

- **PIMPL 隔离**：公开头文件 `fluxcache_sdk.h` 不包含 gRPC、protobuf、内部类型的任何头文件。用户只需链接 `libfluxcache_sdk`，无需关心传递依赖。
- **线程安全**：
  - `FluxCacheSDK` 实例可跨线程共享（内部有锁保护共享状态）
  - `FileHandle` 不保证并发安全（与 POSIX fd 语义一致，单 Handle 单线程使用）
  - 不同 `FileHandle` 可并发使用
- **错误传递**：通过 `Status` / `StatusOr<T>` 返回，不抛异常。错误码对齐 POSIX errno 语义（NOT_FOUND、ALREADY_EXISTS、PERMISSION_DENIED 等）。
- **OpenMode**：`READ_ONLY`、`WRITE_ONLY`、`READ_WRITE`、`APPEND`。

### 2.4 SDKConfig

```cpp
struct SDKConfig {
    std::string master_address;         // Master 地址
    int rpc_timeout_ms = 30000;         // RPC 超时
    int max_retry = 3;                  // 重试次数
    bool local_cache_enabled = true;    // 是否启用 L1 缓存
    size_t local_cache_size_mb = 256;   // L1 缓存容量
};
```

支持从 YAML 配置文件或代码构造。

---

## 3. Client 端本地 Page 缓存

### 3.1 核心数据结构

```cpp
class ClientPageCache {
    struct CachedPage {
        std::shared_ptr<const std::vector<uint8_t>> data;  // Page 数据（通常 1MB）
        int64_t cached_mtime_ms;                           // 缓存时的文件 mtime
    };

    std::unordered_map<PageId, CachedPage> pages_;
    LRUList<PageId> lru_;               // LRU 淘汰链表
    size_t max_size_bytes_;
    size_t current_size_bytes_{0};
    mutable std::shared_mutex mu_;

public:
    explicit ClientPageCache(size_t max_size_bytes);

    // 查询：mtime 匹配返回 shared_ptr（引用计数保证生命周期），miss 返回 nullptr
    std::shared_ptr<const std::vector<uint8_t>> Get(PageId page_id, int64_t expected_mtime);

    // 写入缓存（可能触发 LRU 淘汰）
    void Put(PageId page_id, std::vector<uint8_t> data, int64_t mtime);

    // 主动失效
    void Invalidate(PageId page_id);
    void InvalidateFile(InodeId inode_id);  // 批量清理某文件的所有 Page
    void Clear();

    // 统计
    size_t Size() const;
    size_t Capacity() const;
};
```

### 3.2 LRU 淘汰

- 标准 LRU：Get 命中时提升到链表头部，Put 时插入头部
- 容量超限时从尾部逐个淘汰，直到空间足够
- 淘汰粒度为单个 Page（1MB），内存管理简单可控

### 3.3 线程安全

- `std::shared_mutex`：Get 走 shared_lock（读多写少场景高并发），Put/Invalidate 走 unique_lock
- `Get` 返回 `shared_ptr<const vector<uint8_t>>`，引用计数保证数据生命周期安全：即使 LRU 淘汰或 Invalidate 移除了 pages_ 中的条目，已持有 shared_ptr 的调用方仍可安全访问数据，无悬挂引用风险
- Page 数据使用 `std::vector<uint8_t>` 而非 `std::string`，语义更贴合二进制数据，避免 `std::string` 的 SSO 和 NUL terminator 开销

---

## 4. 数据流

### 4.1 读路径（含 L1 缓存）

```
SDK.Read(path, offset, size):
  1. FileHandle 内已有 inode_id, block_size, ufs_mtime, ufs_uri, ufs_path

  2. 计算目标 Page 列表:
     block_index = offset / block_size
     block_id = MakeBlockId(inode_id, block_index)
     page_start = (offset % block_size) / page_size
     page_count = ceil(size / page_size)

  3. 对每个 page_id = {block_id, page_index}:
     a. ClientPageCache.Get(page_id, ufs_mtime)
        命中 + mtime 匹配 → 直接使用（零网络开销）
     b. Miss 或 mtime 不匹配 → 加入 pending_pages

  4. 如有 pending_pages:
     按 block_id 分组 → hash ring 路由到目标 Worker
     ReadPages(block_id, pending_page_indices, expected_mtime, ufs_uri, ufs_path)

  5. Worker 返回数据后:
     写入 ClientPageCache.Put(page_id, data, ufs_mtime)

  6. 拼接所有 page 数据，按 offset/size 裁剪后返回
```

**短路优化**：如果所有 Page 都命中 L1 缓存，整个读请求零网络 RPC。

### 4.2 写路径

```
SDK.Write(path, offset, data):
  1. 计算 block_id, page_indices

  2. 按 block_id 分组 → hash ring 路由 → Worker
     WritePages(block_id, page_indices, data, ufs_uri, ufs_path)

  3. Worker write-through:
     写入 UFS → mtime 更新 → 更新 Worker PageStore

  4. Worker 返回成功 + 新 mtime

  5. SDK 更新:
     a. ClientPageCache.Put(page_id, data, new_mtime) — 更新 L1 缓存
     b. FileHandle.ufs_mtime = new_mtime — 更新快照
```

### 4.3 文件删除

```
SDK.Delete(path):
  1. Master.DeleteFile(path) → 获取 inode_id
  2. ClientPageCache.InvalidateFile(inode_id) — 清理 L1 缓存
```

---

## 5. 一致性保证

### 5.1 mtime 校验机制

与 Worker 端 PageStore 一致，每个缓存 Page 携带 `cached_mtime_ms`，读取时比对 `expected_mtime`（来自 FileHandle 的 mtime 快照，最初从 Master 获取）。

| 场景 | 行为 |
|---|---|
| L1 命中 + mtime 匹配 | 直接返回，零网络 |
| L1 命中 + mtime 不匹配 | 淘汰旧 Page，走 L2 |
| L1 未命中 | 走 L2 → Worker → 可能走 L3 |
| 文件被外部修改 | 下次 Open/Stat 获取新 mtime → L1 旧 Page 自动失效 |

### 5.2 多实例场景

不同 SDK 实例（不同进程或同一进程多实例）各自维护独立 L1 缓存：
- 通过 mtime 校验保证最终一致
- 每次 Open 或 Stat 从 Master 获取最新 mtime
- 一个实例写入后，其他实例下次 Open 同一文件时获取新 mtime，旧 L1 缓存自动失效

### 5.3 FileHandle 长持有

Open 时获取 mtime 快照，长期持有的 FileHandle 可能用过期 mtime 命中 L1 旧缓存。解决方式：
- 用户可调 `FileHandle::Stat()` 刷新 mtime（主动检查）
- 大多数场景下文件不会在持有期间被修改（FluxCache 定位为缓存系统，非协同编辑）

---

## 6. 配置项

| 配置 | 默认值 | 说明 |
|---|---|---|
| `client.local_cache_enabled` | true | 是否启用 L1 缓存 |
| `client.local_cache_size_mb` | 256 | L1 缓存容量上限（MB） |
| `client.master_address` | — | Master 地址（必填） |
| `client.rpc_timeout_ms` | 30000 | RPC 超时 |
| `client.max_retry` | 3 | 失败重试次数 |

设为 `local_cache_size_mb = 0` 或 `local_cache_enabled = false` 等价于禁用 L1 缓存，所有读写直接走 Worker。

---

## 7. 与现有组件的关系

| 组件 | 关系 |
|---|---|
| P1-07 Client 骨架 | SDK 基于其 Transport Layer 构建 |
| P1-14/P1-15 端到端读写路径 | SDK 的 Read/Write 委托给这些路径的 Client 端实现 |
| P2-09 SDK 基础能力 | 本设计扩展了 P2-09 的 API 范围 |
| P2-11 Client 本地缓存 | 新增 issue，实现 ClientPageCache |
| P1-16 核心类型 | SDK 内部使用 PageId、BlockId 等类型 |
| P1-10 PageStore | L1 缓存设计与 PageStore 的 mtime 校验机制对齐 |

---

## 8. 相关文件

| 文件 | 说明 |
|---|---|
| `src/client/sdk/fluxcache_sdk.h` | SDK 公开头文件 |
| `src/client/sdk/fluxcache_sdk.cpp` | SDK 实现 |
| `src/client/sdk/file_handle.h/.cpp` | FileHandle 实现 |
| `src/client/cache/client_page_cache.h/.cpp` | L1 Page 内存缓存 |
| `src/client/fluxcache_client.h/.cpp` | 内部 Client（Transport Layer） |
| `examples/sdk_example.cpp` | 示例代码 |
| [metadata-design.md](./metadata-design.md) | Master 元数据设计 |
| [block-id-and-file-layout.md](./block-id-and-file-layout.md) | Block/Page 布局设计 |
