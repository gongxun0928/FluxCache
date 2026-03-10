# UFS 抽象层设计

> 日期: 2026-03-11
> 状态: Phase 1（P1-11 实施）
> 关联: issues/P1-11-ufs-localfs.md

## 1. 概述

实现 Under File System (UFS) 最小抽象层，为 FluxCache 提供统一的底层存储访问接口。Phase 1 仅落地 LocalFS 驱动，后续 Phase H 扩展 S3、HDFS 等。

**设计原则**：

- 接口最小化，满足 Phase 1 读写、List、GetStatus、mtime 校验需求
- FileStatus 字段一次定义完整，避免后续缓存一致性返工
- 路径不存在时返回明确错误，不崩溃

## 2. 接口定义

### 2.1 FileStatus

```cpp
struct FileStatus {
  bool exists{false};
  bool is_directory{false};
  uint64_t size{0};
  int64_t mtime_ms{0};   // 毫秒级修改时间，用于 Phase 1 版本校验
  std::string path;
};
```

- `exists`：路径是否存在
- `is_directory`：是否为目录（仅当 exists 为 true 时有效）
- `size`：文件大小（字节），目录为 0
- `mtime_ms`：最后修改时间（毫秒），Phase 1 缓存版本校验来源
- `path`：规范化的绝对路径或相对路径（由实现决定）

### 2.2 UFS 接口

```cpp
class UFS {
 public:
  virtual ~UFS() = default;

  virtual Status Read(const std::string& path, uint64_t offset, uint64_t size,
                      std::string* out) = 0;
  virtual Status Write(const std::string& path, uint64_t offset,
                       std::string_view data) = 0;
  virtual Status GetStatus(const std::string& path, FileStatus* status) = 0;
  virtual Status List(const std::string& path,
                      std::vector<FileStatus>* entries) = 0;
  virtual Status Delete(const std::string& path) = 0;
  virtual Status Rename(const std::string& src, const std::string& dst) = 0;
  virtual Status Mkdirs(const std::string& path) = 0;
};
```

| 方法 | 语义 | 路径不存在 |
|------|------|-------------|
| Read | 从 path 偏移 offset 读取 size 字节 | NotFound |
| Write | 向 path 偏移 offset 写入 data（可扩展文件） | NotFound（父目录存在时创建文件） |
| GetStatus | 获取 path 的 FileStatus | 返回 exists=false，status.ok() |
| List | 列出 path 下子项 | NotFound |
| Delete | 删除文件或空目录 | NotFound |
| Rename | 重命名/移动 | src NotFound |
| Mkdirs | 递归创建目录 | 父目录不存在时创建 |

**GetStatus 路径不存在**：返回 `Status::OK()`，`status->exists = false`。调用方通过 `exists` 判断，而非 Status 码。

### 2.3 UFS Factory

```cpp
Status CreateUFS(const std::string& scheme, const std::string& authority,
                 std::unique_ptr<UFS>* out);
```

- `scheme`：如 `"local"`、`"s3"`（Phase H）
- `authority`：如 LocalFS 的根路径 `/tmp/ufs_root`
- 返回 `NotFound` 或 `InvalidArgument` 表示不支持的 scheme

## 3. LocalFS 实现要点

- **根路径**：构造时指定 `root_path`，所有操作相对于该路径
- **路径规范化**：内部将 path 与 root 拼接，解析 `..`、`.`，拒绝路径穿越
- **mtime 稳定性**：使用 `std::filesystem` 或 POSIX `stat`，写后再次 GetStatus 可获取新 mtime
- **错误映射**：`ENOENT` → GetStatus 返回 exists=false；其他 → IOError/NotFound

## 4. 测试设计

### 4.1 验收策略

| 验收项 | 测试策略 |
|--------|----------|
| 创建、读取、写入、删除 | 在临时目录创建文件，Write 写入，Read 验证，Delete 删除，GetStatus 验证不存在 |
| GetStatus 返回稳定 size/mtime_ms | 写固定内容后 GetStatus，断言 size；写后再次 GetStatus 断言 mtime_ms 已更新（不依赖时间抖动） |
| List 返回子项及基本状态 | Mkdirs 创建目录，Write 创建文件，List 断言条目数量、name、exists、is_directory、size |
| 路径不存在返回明确错误 | GetStatus 不存在的 path → exists=false；Read/List/Delete 不存在的 path → NotFound |
| 不崩溃 | 所有边界路径均有断言，无未定义行为 |

### 4.2 时间抖动规避

- 不依赖 `sleep` 或「写后立即断言 mtime 变化」
- 策略：Write 后再次 GetStatus，用返回的 mtime_ms 作为「当前版本」；或仅断言 size 正确，mtime_ms > 0

### 4.3 非目标

- S3/HDFS 实现（Phase H）
- 符号链接、硬链接
- 权限、owner、group 等扩展属性

## 5. 风险与假设

- **风险**：不同平台 `stat`/`filesystem` mtime 精度不一致。**Mitigation**：Phase 1 仅要求毫秒级，多数平台满足；测试用临时目录避免跨文件系统。
- **假设**：LocalFS 仅用于单机开发/测试，不承诺分布式一致性。

## 6. 变更分级

P1 — 公共接口、核心数据模型（FileStatus）、单服务核心逻辑。
