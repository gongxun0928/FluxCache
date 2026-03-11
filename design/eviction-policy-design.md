# 淘汰策略接口与 LRU 实现设计

> 日期: 2026-03-11
> 状态: Phase F 设计
> 关联: P2-03

## 1. 目标与范围

### 1.1 目标

- 定义可插拔淘汰策略接口，供 PageStore 及后续层级晋升/淘汰流程使用。
- 实现 LRU（Least Recently Used）作为第一种策略。
- 接口设计需支持 LFU 等后续策略复用。

### 1.2 范围

- **在范围内**：EvictionPolicy 接口、LRU 实现、EvictionPolicyFactory。
- **不在范围内**：PageStore 与 EvictionPolicy 的集成（由 P2-02 负责）、LFU 实现（由 P2-04 负责）。

## 2. 接口设计

### 2.1 EvictionPolicy 接口

```cpp
// src/worker/cache/eviction_policy.h
class EvictionPolicy {
 public:
  virtual ~EvictionPolicy() = default;

  // 页插入时调用
  virtual void OnInsert(PageId id) = 0;

  // 页被访问（GetPage 命中）时调用，用于更新淘汰顺序
  virtual void OnAccess(PageId id) = 0;

  // 页被移除时调用
  virtual void OnRemove(PageId id) = 0;

  // 选取下一个应淘汰的页，若无可淘汰则返回无效 PageId
  virtual std::optional<PageId> PickVictim() = 0;
};
```

- `OnInsert`：PutPage 成功后调用。
- `OnAccess`：GetPage 命中后调用，LRU 会将页移到最近使用端。
- `OnRemove`：DeletePage / DeleteBlockPages 后调用。
- `PickVictim`：容量不足或淘汰流程需要时调用，返回最应淘汰的页；空容器时返回 `std::nullopt`。

### 2.2 策略类型枚举

```cpp
enum class EvictionPolicyType : uint8_t {
  kLRU = 0,
  kLFU = 1,  // 预留，P2-04 实现
};
```

### 2.3 EvictionPolicyFactory

```cpp
// src/worker/cache/eviction_policy_factory.h
std::unique_ptr<EvictionPolicy> CreateEvictionPolicy(EvictionPolicyType type);
```

## 3. LRU 实现

### 3.1 数据结构

- **双向链表 + 哈希表**：O(1) 插入、访问、删除、PickVictim。
- 链表头为 MRU（Most Recently Used），尾为 LRU（Least Recently Used）。
- `PickVictim` 返回链表尾元素。

### 3.2 行为

| 操作 | 行为 |
|------|------|
| OnInsert(id) | 将 id 插入链表头 |
| OnAccess(id) | 将 id 从当前位置移到链表头 |
| OnRemove(id) | 从链表和哈希表中移除 id |
| PickVictim() | 返回链表尾的 PageId，若空则 nullopt |

### 3.3 线程安全

Phase 1 不要求线程安全，由调用方（PageStore）保证单线程或外部加锁。

## 4. 验收标准与测试设计

### 4.1 验收标准（对齐 issue）

- [ ] 连续访问后，最久未访问页优先被淘汰。
- [ ] OnAccess 后淘汰顺序会正确变化。
- [ ] 接口可被 LFU 等后续策略复用。
- [ ] 单元测试通过。

### 4.2 测试设计

| 用例 | 描述 | 验证点 |
|------|------|--------|
| PickVictim_Empty_ReturnsNullopt | 空策略 PickVictim | 返回 nullopt |
| PickVictim_SinglePage_ReturnsThatPage | 单页插入后 PickVictim | 返回该页 |
| PickVictim_OrderByInsertion | 多页按序插入，不访问 | 淘汰顺序与插入顺序相反（后插先淘汰） |
| OnAccess_ChangesEvictionOrder | 插入 A,B,C；访问 A；PickVictim | 返回 B（最久未访问） |
| OnRemove_ExcludesFromEviction | 插入 A,B；Remove A；PickVictim | 返回 B |
| Factory_CreatesLRU | Factory 创建 LRU | 行为与直接构造 LruPolicy 一致 |

## 5. 风险与假设

- **风险**：后续 LFU 可能需要额外参数（如容量、衰减因子）。**缓解**：接口保持最小，LFU 可在构造时传入参数。
- **假设**：PageId 在策略生命周期内唯一，同一 PageId 不会重复 OnInsert 未先 OnRemove。
