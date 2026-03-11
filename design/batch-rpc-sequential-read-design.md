# 批量 RPC 与顺序读 Pipeline 设计

> 日期: 2026-03-11
> 状态: P3-03 设计
> 变更分级: P1

## 1. 概述

针对顺序读场景减少 RPC 往返成本，引入批量读取和 pipeline/预读能力。

**目标**：
- 批量读取结果与逐页读取一致（正确性）
- 顺序读吞吐有明确提升
- 随机读不会因预读显著退化
- 基准和回归测试通过

**非目标**：
- 零拷贝、减少复制路径（P3-04）
- 跨 Worker 的分布式预读协调

## 2. 现状分析

### 2.1 当前 Read 流程

- Client 按 block 顺序迭代，每个 block 一次 `ReadPages` RPC
- `ReadPages` 已支持 `repeated page_indices`，单 block 内多页一次 RPC
- 单 Worker 场景：所有 block 路由到同一 Worker，可跨 block 批量
- 多 Worker 场景：同文件不同 block 因 hash 分散到不同 Worker，需按 Worker 分组

### 2.2 瓶颈

- 顺序读 N 个 block → N 次 RPC 往返
- 无预读：block i+1 的 RPC 在 block i 处理完后才发起

## 3. 设计决策

### 3.1 协议扩展：BatchReadPages

新增 `BatchReadPages` RPC，复用 `ReadPagesRequest` 语义，一次请求多 block：

```protobuf
// worker.proto 扩展
rpc BatchReadPages(BatchReadPagesRequest) returns (BatchReadPagesResponse);

message BatchReadPagesRequest {
  repeated ReadPagesRequest requests = 1;  // 每个元素为单 block 请求
}

message BatchReadPagesResponse {
  repeated bytes block_data = 1;  // 与 requests 一一对应
}
```

- 不修改现有 `ReadPages`，保持向后兼容
- `BatchReadPages` 仅用于同一 Worker 的多个 block（Client 按 Worker 分组后调用）

### 3.2 Worker 批量读取

- 实现 `BatchReadPages`：遍历 `requests`，对每个调用现有 PageStore + UFS 逻辑，结果按序填入 `block_data`
- 单 block 请求仍走 `ReadPages`，批量走 `BatchReadPages`

### 3.3 Client 顺序读 Pipeline

1. **按 Worker 分组**：计算 `[first_block, last_block]` 内各 block 的 Worker，合并连续同 Worker 的 block 为一批
2. **批量 RPC**：每批若多 block，调用 `BatchReadPages`；单 block 调用 `ReadPages`
3. **预读**：顺序读时，在返回当前 range 前，异步预取下一 `prefetch_blocks` 个 block（默认 2），填充本地 cache
4. **预读开关**：通过 `ClientConfig.prefetch_blocks` 控制（0=关闭），随机读场景设为 0

### 3.4 配置

```cpp
// ClientConfig 扩展
size_t prefetch_blocks = 2;   // 顺序读预取 block 数，0 关闭
size_t batch_read_max_blocks = 8;  // 单次 BatchReadPages 最大 block 数
```

## 4. 接口影响

| 组件 | 变更 |
|------|------|
| proto | 新增 BatchReadPages RPC 与消息 |
| Worker | 实现 BatchReadPages |
| WorkerClient | 新增 BatchReadPages 方法 |
| FluxCacheClient | Read 内部分组 + 批量 + 预读 |
| ClientConfig | prefetch_blocks, batch_read_max_blocks |

## 5. 测试设计（Design Gate 产出）

### 5.1 正确性

- **批量与逐页一致**：对同一文件、同一 offset/size，`Read` 开启/关闭 batch 和 prefetch，结果字节完全一致
- **回归**：现有 `read_path_e2e_test`、`read_test` 全部通过

### 5.2 性能

- **顺序读基准**：`tests/benchmark/sequential_read_bench.cpp`
  - 大文件顺序读（如 64MB），对比：无 batch/prefetch vs 有 batch vs 有 batch+prefetch
  - 验收：有 batch+prefetch 时吞吐明显高于无优化（如 >1.5x）
- **随机读不退化**：prefetch_blocks=0 时，随机读延迟/吞吐与当前实现相当（允许小幅波动）

### 5.3 边界

- 空 batch、单 block batch 行为正确
- prefetch_blocks=0 时无预读逻辑执行

## 6. 风险与假设

| 风险 | 缓解 |
|------|------|
| 预读导致 cache 污染 | 仅顺序读时启用，且预取量可控 |
| 多 Worker 下 batch 收益有限 | 单 Worker 场景收益最大；多 Worker 仍可减少同 Worker 内 RPC 数 |
| 协议变更影响兼容性 | 新增 RPC，不修改现有 ReadPages |

**假设**：P1-14D、P2-08 已完成，ChannelPool 与重试策略可用。
