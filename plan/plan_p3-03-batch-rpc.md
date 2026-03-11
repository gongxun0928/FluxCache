# Plan: P3-03 批量 RPC 与顺序读 Pipeline

> Status: Done

## Goal

- **Problem**: 顺序读场景下，每 block 一次 RPC，往返成本高；无预读导致流水线空闲。
- **Target outcome**: 批量读取协议扩展、Worker 批量读取、Client 顺序读预读、顺序读基准；批量结果与逐页一致，顺序读吞吐提升，随机读不退化。

## Steps

1. 扩展 proto：在 `worker.proto` 新增 `BatchReadPages` RPC 及 `BatchReadPagesRequest`/`BatchReadPagesResponse`，重新生成 pb 文件。
2. 实现 Worker `BatchReadPages`：在 `WorkerServiceImpl` 中实现，遍历 requests 调用现有 PageStore+UFS 逻辑，按序返回 block_data。
3. 实现 WorkerClient `BatchReadPages`：在 `worker_client.cpp` 中新增方法，复用 ChannelPool 与 RetryPolicy。
4. 扩展 ClientConfig：添加 `prefetch_blocks`、`batch_read_max_blocks` 字段。
5. 实现 Client 批量+预读逻辑：在 `FluxCacheClient::Read` 中按 Worker 分组、批量 RPC、顺序读时预取下一批 block。
6. 实现 `tests/benchmark/sequential_read_bench.cpp`：顺序读基准，对比无优化 vs batch vs batch+prefetch。
7. 添加 benchmark 子目录到 `tests/CMakeLists.txt`。
8. 验证：批量读取与逐页一致、顺序读吞吐提升、随机读不退化、回归测试通过。

## Risks & Assumptions

- **Risk**: 预读导致 cache 污染。**Mitigation**: 仅顺序读启用，prefetch_blocks 可配置为 0。
- **Assumption**: P1-14D、P2-08 已完成；单 Worker 场景下 batch 收益最大。

## To Confirm

- [x] 设计文档 `design/batch-rpc-sequential-read-design.md` 已产出。
- [x] 验收标准以 `issues/P3-03-batch-rpc-pipeline.md` 为准。

## 变更分级

P1 — 涉及 proto 扩展、Worker/Client 核心读路径、新增 benchmark。
