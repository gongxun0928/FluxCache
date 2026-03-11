# 零拷贝与减少复制路径分析

> 日期: 2026-03-11
> 状态: P3-04 设计产物
> 关联: issues/P3-04-zero-copy.md

## 1. 概述

本文档分析 FluxCache 读路径（ReadPages）中的数据复制点，识别热路径上的冗余拷贝，并选择收益明确的优化点落地。不把「零拷贝」作为先验承诺，以实测收益为准。

## 2. 优化前数据复制路径

### 2.1 读路径端到端流程

```
Client.Read(path, offset, size)
  → GetFileInfo
  → 按 Block/Page 切分
  → WorkerClient.ReadPages / BatchReadPages
  → gRPC 网络传输
  → WorkerServiceImpl.ReadPages
  → PageStore.GetPage / UFS.Read
  → 返回 response.data()
  → Client 解析、拼接、裁剪
```

### 2.2 复制点清单（按发生顺序）

| # | 位置 | 操作 | 拷贝量 | 热路径 |
|---|------|------|--------|--------|
| C1 | MemoryTier::Read | `out->assign(block.data()+offset, size)` | 1× page_size | ✓ |
| C2 | FileTier::Read | `f.read(out->data(), size)` | 0（直接读入 out） | ✓ |
| C3 | Worker ReadPages 循环 | `concatenated += page_data` | N× page_size（每页追加） | ✓ |
| C4 | Worker ReadPages | `response->set_data(std::move(concatenated))` | 0（move） | - |
| C5 | gRPC/protobuf | 序列化 bytes 到网络缓冲区 | 1× 总大小 | ✓ |
| C6 | Client 解析 | `std::string chunk = data.substr(pos, chunk_len)` | N× page_size | ✓ |
| C7 | Client 存储 | `st->page_data[pi] = chunk` | N× page_size | ✓ |
| C8 | Client 缓存 | `std::vector<uint8_t> vec(chunk.begin(), chunk.end())` | N× page_size（有 cache 时） | 可选 |
| C9 | Client 组装 | `assembled += it->second` | 多页时多次 | ✓ |
| C10 | Client 裁剪 | `result += assembled.substr(skip, take)` | 1× 最终结果 | ✓ |

### 2.3 热路径复制汇总

**Worker 侧（单次 ReadPages，N 页）：**

- MemoryTier 命中：N 次 `assign`（每页 1MB 默认）
- 多页时：`concatenated += page_data` 导致 N 次追加，每次可能触发 realloc + 拷贝

**Client 侧：**

- `data.substr(pos, chunk_len)` 为每页创建新 string
- `page_data[pi] = chunk` 再次拷贝
- 有 L1 cache 时：`vec(chunk.begin(), chunk.end())` 又一次拷贝

**单页读（常见随机读场景）简化链：**

1. PageStore.GetPage → MemoryTier::Read → `out->assign`（1 次）
2. Worker: `concatenated += page_data`（1 次，可消除）
3. response->set_data(move)
4. Client: substr → page_data（2 次）
5. assembled += page_data → result += substr（2 次）

## 3. 候选优化方案

### 3.1 方案 A：Worker ReadPages 单页快速路径（推荐落地）

**描述**：当 `page_indices().size() == 1` 时，跳过 `concatenated` 中间变量，直接将 `GetPage` 得到的 `page_data` 通过 `std::move` 设置到 `response->set_data()`。

**收益**：消除 C3 在单页场景下的一次拷贝（page_data → concatenated）。

**实现复杂度**：低，约 15 行代码。

**风险**：无，逻辑等价，仅减少一次 string 拷贝。

### 3.2 方案 B：BatchReadPages 单页 per-request 快速路径

**描述**：BatchReadPages 内每个 request 若只有 1 页，同样走直接 move 路径。

**收益**：与 A 类似，批量 RPC 中单页 block 的优化。

**实现**：在 BatchReadPages 循环内判断 `req.page_indices().size() == 1` 时单独处理。

### 3.3 方案 C：Client 端避免 substr 拷贝

**描述**：用 `std::string_view` 表示 chunk，仅在需要写入 cache 或 `page_data` 时再 materialize。

**收益**：减少 C6、C7 的中间拷贝。

**复杂度**：中高。`page_data` 为 `map<uint32_t, string>`，最终组装需要连续内存；若用 string_view，需保证底层 `read_resp.data()` 生命周期覆盖整个解析与组装过程。当前流程中 `read_resp` 在 `flush_batch` 返回前有效，可满足。但 `assembled += view` 会触发拷贝，最终 `result` 仍需连续数据。收益有限，且需重构数据结构。

**结论**：暂不落地，留作后续优化。

### 3.4 方案 D：gRPC/protobuf 零拷贝

**描述**：使用 `grpc::ByteBuffer`、`Arena` 或 `string* mutable_data()` 等避免序列化时的额外拷贝。

**复杂度**：高，涉及 gRPC 与 protobuf 内部实现，且需评估与现有 RPC 契约的兼容性。

**结论**：本阶段不落地，仅作调研记录。

## 4. 选定优化：方案 A

**落地项**：Worker ReadPages 单页快速路径。

**验收标准**：

- 单页请求时，`page_data` 直接 `std::move` 到 `response->set_data()`，无 `concatenated += page_data`。
- 多页请求行为不变。
- 功能回归测试通过。
- 基准对比：单页读场景下，提供优化前后吞吐/延迟数据。

## 5. 测试设计

### 5.1 功能回归

- 现有 `ReadPagesTest`：FirstReadMissesThenFillsCache、MtimeMismatchEvictsAndRefetches、MultiPageReturnsInOrder 等保持通过。
- 新增：单页 ReadPages 返回正确内容（覆盖快速路径）。

### 5.2 基准对比

- 复用 `sequential_read_bench` 或新增 `read_pages_copy_bench`：
  - 场景 1：单页随机读（多 block，每 block 读 1 页），对比优化前后 QPS/延迟。
  - 场景 2：多页顺序读，验证无回归。

### 5.3 验收策略

- 单页路径：通过单元测试验证数据正确性。
- 性能：提供优化前后 benchmark 输出，单页读有可观测提升或至少无退化。

## 6. 风险与假设

| 风险 | 等级 | 缓解 |
|------|------|------|
| 单页路径与多页路径逻辑分叉导致遗漏 | 低 | 单页路径复用相同 UFS fallback、mtime 校验逻辑 |
| 基准噪声大，无法体现差异 | 低 | 多次迭代取平均，单页读场景放大拷贝占比 |

**假设**：单页读在随机读、小 IO 场景下占一定比例，优化具有实际意义。

## 7. 非目标

- 不改造 protobuf/gRPC 序列化层。
- 不改造 Client 端 substr/page_data 结构（方案 C 留作后续）。
- 不承诺「零拷贝」全链路，仅消除已识别的热路径冗余拷贝。
