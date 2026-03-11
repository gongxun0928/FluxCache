# Plan: P1-14C Client.Read 最小实现

> Status: Done

## Goal

- **Problem**: Client 需要实现文件级读路径，调用 GetFileInfo 获取元数据，按 Block/Page 切片，选 Worker，发送 ReadPages，拼接并裁剪返回。
- **Target outcome**: `FluxCacheClient::Read(path, offset, size)` 返回正确数据；支持跨 page、跨 block 切分；失败时按最新 ring_version 刷新一次后重试。

## Design 要点

### 1. 读路径流程

```
Read(path, offset, size):
  1. MasterClient::GetFileInfo(path) → FileInfo, ring_version, workers, ufs_uri, ufs_path
  2. 用 GetFileInfoResponse 更新 CachedHashRing（ring_version + workers）
  3. 校验 offset/size，clamp 到 file_size
  4. 按 block_size 计算涉及的 block 范围 [first_block_idx, last_block_idx]
  5. 对每个 block:
     a. 计算 block 内需读取的 page 范围（含首尾部分 page 裁剪）
     b. GetWorkerForBlock(block_id) → worker_id
     c. GetWorkerClient(worker_id) → WorkerClient
     d. ReadPages(block_id, page_indices, expected_mtime_ms, ufs_uri, ufs_path)
     e. 失败时: RefreshRing() 一次，重试
     f. 从返回的 data 中裁剪出本 block 贡献的 [overlap_start, overlap_end) 字节，追加到 result
  6. 返回 result
```

### 2. Block/Page 切分

- `block_size`、`page_size` 来自 FileInfo.block_size 与 ClientConfig.page_size（默认 1MB）
- 文件内 block 范围: `first_block_idx = offset / block_size`, `last_block_idx = (offset + size - 1) / block_size`
- 每个 block 内 overlap: `overlap_start = max(offset, block_start)`, `overlap_end = min(offset+size, block_end)`
- Page 范围: `first_page = (overlap_start - block_start) / page_size`, `last_page = (overlap_end - block_start - 1) / page_size`
- 裁剪: Worker 返回拼接的 page 数据，需跳过 `skip = (overlap_start - block_start) % page_size`，取 `len = overlap_end - overlap_start` 字节

### 3. 重试策略

- ReadPages 失败（Unavailable 等）时: 调用 `RefreshRing()` 获取最新 ring_version，再重试一次
- 重试仅一次，不递归

### 4. 接口变更

| 组件 | 变更 |
|------|------|
| `MasterClient` | 新增 `StatusOr<GetFileInfoResponse> GetFileInfo(path)` |
| `ClientConfig` | 新增 `page_size`（默认 1MB），用于 Client 切分 |
| `FluxCacheClient` | 新增 `StatusOr<std::string> Read(path, offset, size)` |

### 5. 错误码

| 场景 | 返回 |
|------|------|
| GetFileInfo 失败 | 透传 Status |
| offset >= file_size | 返回空字符串 |
| 无 Worker | NotFound |
| ReadPages 失败且重试后仍失败 | 透传 Status |

## Steps

1. 在 `MasterClient` 中实现 `GetFileInfo(path)`。
2. 在 `ClientConfig` 中新增 `page_size`（默认 1MB）。
3. 在 `FluxCacheClient` 中实现 `Read(path, offset, size)` 主逻辑（含 block/page 切分、选 Worker、ReadPages、裁剪、重试）。
4. 创建 `tests/client/read_test.cpp`，使用小尺寸配置（block_size=2MB、page_size=1MB）验证单页、跨页、跨 block。
5. 执行 Testing Gate：构建、ctest -R read、lint。

## Risks & Assumptions

- **Risk**: ClientConfig.page_size 与 Worker 配置不一致会导致读错。**Mitigation**: 测试中 Master/Worker/Client 使用相同小尺寸；文档中说明集群内 page_size 需一致。
- **Assumption**: Phase 1 不引入 L1 缓存，Read 直接走 Worker。
- **Assumption**: GetFileInfoResponse 的 workers 与 GetHashRingResponse 结构兼容，可构建 GetHashRingResponse 后调用 CachedHashRing::Update。

## 测试设计

- **单页内读取**: 文件 size <= page_size，offset=0，size=file_size，返回完整内容。
- **跨页读取**: 文件 size = 2*page_size，offset=0，size=2*page_size，返回正确拼接；offset=page_size/2，size=page_size，验证首尾裁剪。
- **跨 block 读取**: block_size=2MB、page_size=1MB，文件 size=3MB，offset=0，size=3MB，验证跨 block 拼接正确。
- **边界**: offset >= file_size 返回空；offset+size > file_size 时 clamp。

## 变更分级

P1 — 涉及 Client 公共接口、MasterClient 扩展、RPC 调用。

## To Confirm

- [x] 验收标准以 issues/P1-14c-client-read-path.md 为准。
- [x] 测试使用小尺寸配置验证边界。
