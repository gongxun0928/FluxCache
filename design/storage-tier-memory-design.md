# Storage Tier 与 Memory Tier 设计

> 日期: 2026-03-11
> 状态: Phase 1（P1-09 实施）
> 关联: issues/P1-09-storage-tier-memory.md

## 1. 概述

实现 Worker 端的最小存储层抽象与 Memory tier。仅负责页级数据块的分配、读写、释放和容量统计，不直接感知分布式 block 路由。

**设计原则**：

- 接口最小化，满足 Phase 1 分配、读写、释放、容量统计需求
- 容量耗尽时返回明确错误（ResourceExhausted）
- 释放后容量正确回收，可观测

## 2. 接口定义

### 2.1 TierBlockHandle

分配成功后返回的句柄，用于后续读写与释放。句柄不暴露内部实现，仅作为 opaque 标识。

```cpp
struct TierBlockHandle {
  uint64_t id{0};  // 内部分配 ID，0 表示无效

  bool valid() const { return id != 0; }
};
```

- `id == 0` 表示无效句柄
- 实现内部维护 id → 实际内存块的映射

### 2.2 StorageTier 抽象

```cpp
class StorageTier {
 public:
  virtual ~StorageTier() = default;

  // 分配 size 字节的数据块，返回句柄；容量不足时返回 ResourceExhausted
  virtual Status Allocate(size_t size, TierBlockHandle* handle) = 0;

  // 向 handle 指向的块写入 data（从 offset 开始，长度 data.size()）
  virtual Status Write(const TierBlockHandle& handle, size_t offset,
                       std::string_view data) = 0;

  // 从 handle 指向的块读取 [offset, offset+size)，写入 out
  virtual Status Read(const TierBlockHandle& handle, size_t offset, size_t size,
                      std::string* out) = 0;

  // 释放块，回收容量
  virtual Status Release(const TierBlockHandle& handle) = 0;

  // 当前已用容量（字节）
  virtual size_t UsedCapacity() const = 0;

  // 容量上限（字节）
  virtual size_t CapacityLimit() const = 0;
};
```

| 方法 | 语义 | 错误条件 |
|------|------|----------|
| Allocate | 分配 size 字节块 | 容量不足 → ResourceExhausted；size==0 → InvalidArgument |
| Write | 向块写入 | 无效 handle / 越界 → InvalidArgument |
| Read | 从块读取 | 无效 handle / 越界 → InvalidArgument |
| Release | 释放块 | 无效 handle → InvalidArgument |
| UsedCapacity | 已用容量 | 无 |
| CapacityLimit | 容量上限 | 无 |

### 2.3 Status 扩展

容量耗尽需返回明确错误。在 `StatusCode` 中新增：

```cpp
enum class StatusCode : uint8_t {
  kOk = 0,
  kNotFound,
  kIOError,
  kInvalidArgument,
  kResourceExhausted,  // 容量耗尽
};
```

## 3. MemoryTier 实现要点

- **存储**：`std::unordered_map<uint64_t, std::string>`，id → 块内容
- **容量**：构造时传入 `capacity_limit`，分配时检查 `used_ + size <= capacity_limit`
- **释放**：从 map 移除，`used_ -= block_size`
- **线程安全**：Phase 1 不要求，接口设计预留后续加锁空间

### 3.1 分配逻辑

```
if (size == 0) return InvalidArgument;
if (used_ + size > capacity_limit_) return ResourceExhausted;
id = next_id_++;
blocks_[id] = std::string(size, '\0');
used_ += size;
handle->id = id;
return OK;
```

### 3.2 读写逻辑

- Write：校验 handle 有效、offset + data.size() <= block_size，memcpy 到块内
- Read：校验 handle 有效、offset + size <= block_size，从块拷贝到 out

### 3.3 释放逻辑

```
if (!handle.valid()) return InvalidArgument;
auto it = blocks_.find(handle.id);
if (it == blocks_.end()) return InvalidArgument;
used_ -= it->second.size();
blocks_.erase(it);
return OK;
```

## 4. 目录结构

```
src/worker/storage/
  storage_tier.h      # StorageTier 接口 + TierBlockHandle
  memory_tier.h
  memory_tier.cpp
```

worker 模块通过 `add_subdirectory(storage)` 或直接添加 storage 源文件到 fluxcache_worker。

## 5. 测试设计（验收策略）

| 验收项 | 测试用例 | 断言 |
|--------|----------|------|
| 分配指定大小并写入/读取 | AllocateWriteRead | Allocate 成功 → Write 成功 → Read 内容一致 |
| 容量达到上限时返回明确错误 | CapacityExhausted | 分配至满 → 再分配返回 ResourceExhausted |
| 释放后容量正确回收 | ReleaseReclaimsCapacity | 分配 → 释放 → UsedCapacity 减少；可再次分配 |
| 单元测试覆盖 | AllocateWriteRead, CapacityExhausted, ReleaseReclaimsCapacity, InvalidHandle | 覆盖主路径与边界 |

### 5.1 用例清单

1. **AllocateWriteRead**：分配 1KB → 写入 "hello" → 读取 → 断言相等
2. **CapacityExhausted**：limit=100，分配 60 → 分配 50 成功 → 再分配 1 失败，StatusCode::kResourceExhausted
3. **ReleaseReclaimsCapacity**：分配 100 → Release → UsedCapacity==0 → 再分配 100 成功
4. **InvalidHandle**：对 id=0 的 handle 调用 Write/Read/Release → InvalidArgument
5. **WriteReadOffset**：分配 16 字节 → Write(offset=4, "test") → Read(offset=4, 4) → 得 "test"

## 6. 非目标

- 不实现 SSD/HDD tier
- 不实现 PageStore 集成
- 不实现分布式 block 路由
- 不实现并发安全（Phase 1 单线程）

## 7. 风险与假设

- **风险**：Status 新增枚举影响现有调用方。**Mitigation**：新增值为扩展，现有代码不依赖枚举穷举。
- **假设**：MemoryTier 仅用于 Worker 进程内，无跨进程共享。
