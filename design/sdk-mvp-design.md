# P2-09 C++ SDK MVP 设计

> 状态: P2-09 实现依据
> 参考: [client-sdk-design.md](./client-sdk-design.md)

## 1. 目标与边界

### 1.1 目标

在 FluxCacheClient 之上提供面向应用开发者的 C++ SDK MVP，完成文件级主路径：Create → Open → Write → Read → Stat → Delete。

### 1.2 非目标（MVP 不支持的 namespace API）

以下能力明确后移，**不属于 MVP 验收范围**，文档需明确写出避免用户误用：

- `Rename`
- `List`
- `Mkdir`
- `Rmdir`
- `Exists`

### 1.3 设计约束

- SDK 头文件**不暴露** gRPC、protobuf 等内部依赖（PIMPL 隔离）
- 错误通过 `Status` / `StatusOr<T>` 传递，**不抛异常**
- 内部封装 FluxCacheClient，复用已有 Read/Write/CreateFile/GetFileInfo 能力

## 2. 接口设计

### 2.1 types.h（公开类型）

```cpp
// 仅依赖 common/status.h、common/status_or.h，无 gRPC/protobuf
struct SDKConfig {
  std::string master_host;
  uint16_t master_port = 0;
  size_t page_size = 1024 * 1024;
  size_t channel_pool_size = 4;
  int retry_max_attempts = 3;
  int retry_initial_delay_ms = 50;
};

struct FileInfo {
  uint64_t inode_id = 0;
  uint64_t size = 0;
  bool is_directory = false;
  int64_t ufs_mtime_ms = 0;
};

enum class OpenMode { kReadOnly, kWriteOnly, kReadWrite };
```

### 2.2 FluxCacheSDK

```cpp
class FluxCacheSDK {
 public:
  static StatusOr<std::unique_ptr<FluxCacheSDK>> Create(const SDKConfig& config);
  ~FluxCacheSDK();

  Status Create(const std::string& path);
  StatusOr<std::unique_ptr<FileHandle>> Open(const std::string& path, OpenMode mode);
  Status Delete(const std::string& path);
  StatusOr<FileInfo> Stat(const std::string& path);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
```

### 2.3 FileHandle

```cpp
class FileHandle {
 public:
  ~FileHandle();
  StatusOr<size_t> Read(void* buf, uint64_t offset, size_t size);
  Status Write(uint64_t offset, std::string_view data);
  StatusOr<FileInfo> Stat();
  Status Close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
```

### 2.4 与 FluxCacheClient 映射

| SDK API | 内部调用 |
|---------|----------|
| Create(path) | MasterClient::CreateFile(path) |
| Open(path, mode) | MasterClient::GetFileInfo(path) → 构造 FileHandle |
| Read | FluxCacheClient::Read(path, offset, size) |
| Write | FluxCacheClient::Write(path, offset, data) |
| Stat(path) | MasterClient::GetFileInfo(path) → 转 FileInfo |
| Delete(path) | MasterClient::DeleteFile(path) |

## 3. 实现要点

### 3.1 PIMPL 隔离

- `fluxcache_sdk.h`、`file_handle.h`、`types.h` 仅包含标准库与 `common/status.h`、`common/status_or.h`
- 所有 gRPC、protobuf、FluxCacheClient 等依赖仅在 `.cpp` 中 include

### 3.2 StatusOr 复用

- 新增 `common/status_or.h`，仅依赖 `common/status.h` 与 `<optional>`
- `common/config/config.h` 改为使用 `status_or.h` 的 StatusOr
- SDK 使用 `common/status_or.h`

### 3.3 Delete 语义

- Master 已有 DeleteFile RPC 契约，当前 MasterServiceImpl 返回 UNIMPLEMENTED
- MasterClient 需新增 `DeleteFile(path)`，SDK Delete 调用之
- 服务端未实现时，SDK Delete 将返回 Unavailable 或 IOError，符合“错误通过 Status 传递”

## 4. 风险与假设

| 风险 | 缓解 |
|------|------|
| DeleteFile 服务端未实现 | SDK 提供 API 表面，调用时返回错误；后续 Master 实现后即可工作 |
| StatusOr 分散定义 | 统一到 common/status_or.h |

**假设**：P1-14D、P1-15D、P2-08 已完成，Read/Write 路径稳定。

## 5. 验收标准（与 issue 对齐）

- [ ] SDK 头文件不暴露 gRPC、protobuf 等内部依赖
- [ ] 通过 SDK 可完成文件级主路径：Create → Open → Write → Read → Stat → Delete
- [ ] 错误通过 Status/StatusOr<T> 传递，不抛异常
- [ ] 示例代码可编译运行
- [ ] 文档明确写出 MVP 不支持的 namespace API

## 6. 测试设计

### 6.1 单元测试（sdk_test.cpp）

- **Create**：调用 Create 后 Stat 可获取文件元数据
- **Open**：对已存在文件 Open 成功，返回非空 FileHandle
- **Read**：Create → Write → Read，内容一致
- **Write**：覆盖写、追加写
- **Stat**：返回正确的 inode_id、size、is_directory、ufs_mtime_ms
- **Delete**：Delete 后 Stat 返回 NotFound（或服务端未实现时返回相应错误）
- **错误路径**：Open 不存在的文件返回 NotFound；Create 已存在文件返回 AlreadyExists
- **头文件隔离**：sdk_test 仅 include SDK 公开头文件，不直接 include gRPC/protobuf

### 6.2 集成验证

- 使用 E2E 环境（Master + Worker + FakeUfs）运行 sdk_test，验证 Create → Open → Write → Read → Stat 主路径
- Delete 在服务端未实现时，测试预期返回非 ok 的 Status

### 6.3 示例程序

- `examples/sdk_example.cpp` 演示完整流程，可配置 master 地址，编译为独立可执行文件
