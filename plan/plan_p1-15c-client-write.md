# Plan: P1-15C Client.Write 最小实现

> Status: Done

## Goal

- **Problem**: Client 需要实现文件级写路径，对新文件走 CreateFile、对已存在文件走 GetFileInfo，按 Block/Page 切片，选 Worker，发送 WritePages，成功后调用 CompleteFile 更新元数据。
- **Target outcome**: `FluxCacheClient::Write(path, offset, data)` 正确写入；支持跨 page、跨 block 切分；写入失败时不调用 CompleteFile。

## Design 要点

### 1. 写路径流程

```
Write(path, offset, data):
  1. 获取 FileInfo: GetFileInfo(path) 或 CreateFile(path)
     - GetFileInfo 成功 → 已存在文件，使用返回的 FileInfo
     - GetFileInfo 返回 NotFound → CreateFile(path) → 新文件
  2. 用 GetFileInfoResponse / CreateFileResponse 更新 CachedHashRing
  3. 计算 new_size = max(current_size, offset + data.size())
  4. 按 block_size 计算涉及的 block 范围 [first_block_idx, last_block_idx]
  5. 对每个 block:
     a. 计算 block 内需写入的 page 范围（含首尾部分 page 填充）
     b. 按 page 顺序拼接 data（不足整页补零）
     c. GetWorkerForBlock(block_id) → worker_id
     d. GetWorkerClient(worker_id) → WorkerClient
     e. WritePages(block_id, page_indices, data, ufs_uri, ufs_path)
     f. 若任一 WritePages 失败 → 直接返回错误，不调用 CompleteFile
  6. 全部 WritePages 成功后：CompleteFile(inode_id, new_size, ufs_mtime_ms)
     - ufs_mtime_ms 来自最后一次 WritePagesResponse
  7. 返回 Status::OK
```

### 2. Block/Page 切分（与 Read 对称）

- 写入范围: [offset, offset + data.size())
- first_block_idx = offset / block_size
- last_block_idx = (offset + data.size() - 1) / block_size
- 每个 block 内: overlap_start = max(offset, block_start), overlap_end = min(offset + data.size(), block_end)
- Page 范围: first_page = (overlap_start - block_start) / page_size, last_page = (overlap_end - block_start - 1) / page_size
- 部分页处理: 首页/末页可能不足整页，需用零填充到 page_size 再发送

### 3. 失败与回滚

- **WritePages 失败**：直接返回错误，**不调用 CompleteFile**，避免提交错误元数据。
- **CompleteFile 失败**：UFS 已写入，数据在 UFS 中；返回错误，Client 可重试 CompleteFile。

### 4. 接口变更

| 组件 | 变更 |
|------|------|
| `MasterClient` | 新增 `CreateFile(path)`, `CompleteFile(inode_id, size, mtime)` |
| `Worker WritePagesResponse` | 新增 `ufs_mtime_ms`（Worker 写入后 GetStatus 获取） |
| `FluxCacheClient` | 新增 `Status Write(path, offset, data)` |

### 5. 错误码

| 场景 | 返回 |
|------|------|
| GetFileInfo 失败且非 NotFound | 透传 Status |
| CreateFile 失败 | 透传 Status |
| 无 Worker | NotFound |
| WritePages 失败 | 透传 Status，**不调用 CompleteFile** |
| CompleteFile 失败 | 透传 Status |

## Steps

1. 扩展 `WritePagesResponse` 增加 `ufs_mtime_ms`，Worker 写入后设置。
2. 在 `MasterClient` 中实现 `CreateFile(path)` 和 `CompleteFile(inode_id, size, mtime)`。
3. 在 `FluxCacheClient` 中实现 `Write(path, offset, data)` 主逻辑。
4. 创建 `tests/client/write_test.cpp`，验证单页、跨页、跨 block、失败不 CompleteFile。
5. 执行 Testing Gate：构建、ctest -R write、lint。

## Risks & Assumptions

- **Assumption**: Phase 1 不引入 L1 缓存，Write 直接走 Worker。
- **Assumption**: 部分页写入时，首尾页需零填充到 page_size；Worker 要求 data 长度 = page_indices.size() * page_size。
- **Risk**: 多 block 写入时，若部分成功、部分失败，UFS 可能处于不一致状态。**Mitigation**：最小实现按 block 顺序写，首失败即返回；不承诺部分回滚。

## 测试设计

- **单页写入**: 文件 size <= page_size，offset=0，写入后 Read 返回正确内容。
- **跨页写入**: 跨 2 页，写入后 Read 返回正确拼接。
- **跨 block 写入**: block_size=2MB、page_size=1MB，文件跨 2 block，写入后 Read 正确。
- **失败不 CompleteFile**: FakeUfs.SetWriteFail(true)，Write 失败，断言 CompleteFile 未被调用（通过 Master 侧 GetFileInfo 验证 size 未更新）。

## 变更分级

P1 — 涉及 Client 公共接口、MasterClient 扩展、Worker proto 扩展、RPC 调用。

## To Confirm

- [x] 验收标准以 issues/P1-15c-client-write-path.md 为准。
- [x] 测试使用小尺寸配置验证边界。
