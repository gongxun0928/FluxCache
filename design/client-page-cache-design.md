# Client 本地 Page 内存缓存设计

> 日期: 2026-03-11
> 状态: P2-11 设计
> 关联: [client-sdk-design.md](./client-sdk-design.md) §3

## 1. 目标与范围

### 1.1 目标

在 SDK 内部提供可选的 L1 Page 内存缓存，使用与 Worker PageStore 一致的 `mtime` 校验机制，减少读路径网络往返。

### 1.2 范围

- **在范围内**：ClientPageCache 类、Get/Put/Invalidate/InvalidateFile/Clear、LRU 淘汰、SDK 集成、local_cache_enabled 开关。
- **不在范围内**：LFU 等其它淘汰策略、跨进程共享缓存。

## 2. 接口设计

### 2.1 ClientPageCache

```cpp
// src/client/cache/client_page_cache.h
class ClientPageCache {
 public:
  explicit ClientPageCache(size_t max_size_bytes);

  // Get: 命中且 mtime 匹配返回数据；mtime 不匹配返回 nullptr 并淘汰旧页
  std::shared_ptr<const std::vector<uint8_t>> Get(PageId page_id,
                                                   int64_t expected_mtime_ms);

  // Put: 写入缓存，可能触发 LRU 淘汰
  void Put(PageId page_id, std::vector<uint8_t> data, int64_t mtime_ms);

  void Invalidate(PageId page_id);
  void InvalidateFile(InodeId inode_id);
  void Clear();

  size_t Size() const;
  size_t Capacity() const;

 private:
  struct CachedPage;
  // ... LRU 数据结构
};
```

### 2.2 mtime 校验

| 场景 | 行为 |
|------|------|
| 命中 + mtime 匹配 | 返回 data，更新 LRU 顺序 |
| 命中 + mtime 不匹配 | 淘汰旧页，返回 nullptr（调用方走 Worker） |
| 未命中 | 返回 nullptr |

### 2.3 LRU 淘汰

- 双向链表 + unordered_map，O(1) 操作
- Put 时若 `current_size + page_size > max_size`，从尾部逐个淘汰直到空间足够
- Get 命中时移到链表头（MRU）

## 3. SDK 集成

### 3.1 SDKConfig 扩展

```cpp
struct SDKConfig {
  // ... existing
  bool local_cache_enabled = true;
  size_t local_cache_size_mb = 256;
};
```

### 3.2 FluxCacheClient 集成

- ClientConfig 增加 `local_cache_enabled`、`local_cache_size_mb`
- FluxCacheClient 持有 `std::unique_ptr<ClientPageCache> cache_`（可为 nullptr）
- `local_cache_enabled=false` 或 `local_cache_size_mb=0` 时 `cache_` 为 nullptr

### 3.3 读路径集成

```
FluxCacheClient::Read:
  for each block/page:
    if (cache_ && cache_->Get(page_id, expected_mtime_ms)):
      使用缓存数据
    else:
      Worker.ReadPages(...)
      if (cache_): cache_->Put(page_id, data, expected_mtime_ms)
```

### 3.4 写路径

- Write 成功后可选 Put 到 L1（设计文档建议更新 L1），本 issue 先实现读路径 L1 命中，写路径 Put 可选。

### 3.5 Delete 路径

- Delete 成功后 `cache_->InvalidateFile(inode_id)`（需从 GetFileInfo 获取 inode_id）

## 4. 测试设计

### 4.1 单元测试（client_page_cache_test.cpp）

| 用例 | 验收点 |
|------|--------|
| GetHitMtimeMatch | 命中且 mtime 匹配时返回缓存数据 |
| GetHitMtimeMismatch | mtime 不匹配时返回 miss，旧页被淘汰 |
| PutEvictsLru | 容量超限时按 LRU 淘汰 |
| Invalidate | Invalidate(page_id) 移除指定页 |
| InvalidateFile | InvalidateFile(inode_id) 移除该文件所有页 |
| Clear | Clear 清空缓存 |

### 4.2 集成测试（sdk_test 扩展）

| 用例 | 验收点 |
|------|--------|
| FirstReadGoesToWorker | local_cache_enabled=true，首次读走 Worker |
| SecondReadHitsL1 | 二次读同一区间命中 L1，零 RPC（可通过 mock 或计数验证） |
| LocalCacheDisabled | local_cache_enabled=false 时缓存不生效，每次读都走 Worker |

### 4.3 验证策略

- 单元测试：纯内存，不依赖服务端
- 集成测试：启动 Master+Worker，通过 Read 两次相同 offset/size 验证 L1 命中（可注入 RPC 计数或使用 FakeUfs 观察）

## 5. 风险与假设

- **线程安全**：Phase 1 单线程或简单多线程，使用 `std::shared_mutex` 保护
- **Page 粒度**：与 Worker 一致，默认 1MB
- **local_cache_size_mb=0**：等价于 local_cache_enabled=false
