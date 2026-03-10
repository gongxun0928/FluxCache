# Plan: P1-07 Client RPC 封装与本地路由缓存骨架

> Status: Done

## Goal

- **Problem**: Client 需要与 Master/Worker 通信，复用 ChannelPool，设置 RPC deadline，并本地缓存 hash ring 以按 BlockId 路由到 Worker。
- **Target outcome**: 实现 Client 侧最小 transport 外壳：Master/Worker stub 封装、deadline 设置、本地 ring 缓存和按 BlockId 选择 Worker 的能力。不实现完整读写主路径（由 P1-14C/P1-15C 承担）。

## Design 要点

### 1. 模块划分

| 模块 | 职责 |
|------|------|
| `MasterClient` | 通过 ChannelPool 创建 Master stub，封装 GetHashRing 等 RPC，统一设置 deadline |
| `WorkerClient` | 通过 ChannelPool 创建 Worker stub（按地址），封装 ReadPages/WritePages，统一设置 deadline |
| `CachedHashRing` | 本地 ring 缓存，根据 ring_version 初始化/刷新，提供 GetWorker(BlockId)、GetWorkerAddress(WorkerId) |
| `FluxCacheClient` | 门面：持有 ChannelPool、MasterClient、CachedHashRing，提供 GetWorkerForBlock、GetWorkerClient |

### 2. 接口设计

**MasterClient**
- `MasterClient(ChannelPool* pool, const std::string& master_address, int deadline_sec = 10)`
- `StatusOr<GetHashRingResponse> GetHashRing()` — 所有 RPC 设置 deadline

**WorkerClient**
- `WorkerClient(ChannelPool* pool, const std::string& worker_address, int deadline_sec = 10)`
- `Status ReadPages(...)` / `Status WritePages(...)` — 骨架接口，Phase 1 可只实现 GetHashRing 相关验证

**CachedHashRing**
- `void Update(const GetHashRingResponse& resp)` — 根据 ring_version 与 workers 更新本地 ring
- `uint64_t GetVersion() const`
- `WorkerId GetWorker(BlockId block_id) const` — 返回 0 表示无 Worker
- `std::string GetWorkerAddress(WorkerId worker_id) const` — 返回 "host:port"，空表示未找到

**FluxCacheClient**
- `FluxCacheClient(const ClientConfig& config)` — 从 config 取 master_host:master_port
- `StatusOr<WorkerId> GetWorkerForBlock(BlockId block_id)` — 先 RefreshRing 若需要，再 GetWorker
- `StatusOr<std::unique_ptr<WorkerClient>> GetWorkerClient(WorkerId worker_id)` — 根据 ring 查地址，创建 WorkerClient

### 3. Deadline 策略

- 所有 RPC 调用通过 `grpc::ClientContext::set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(N))` 设置。
- 默认 10 秒，可配置（Phase 1 可写死）。

### 4. 错误码映射（稳定）

| 场景 | 返回 |
|------|------|
| 无 Worker（ring 空或 GetWorker 返回 0） | `Status::NotFound("no worker in ring")` |
| Master 不可达（GetHashRing DEADLINE_EXCEEDED/UNAVAILABLE） | `Status::Unavailable(msg)` |
| Worker 不可达（ReadPages/WritePages 失败） | `Status::Unavailable(msg)` |

需在 `Status` 中新增 `StatusCode::kUnavailable` 与 `Status::Unavailable()`。

### 5. CachedHashRing 实现

- 内部维护 `ring_version_`、`workers_`（worker_id -> WorkerEndpointInfo）
- 使用与 `HashRingManager` 相同的 FNV1a 哈希逻辑，按 block_id 计算顺时针第一个 Worker
- Phase 1 单 Worker 场景：ring 仅一个节点，GetWorker 恒返回该 Worker

## Steps

1. 在 `Status` 中新增 `kUnavailable` 与 `Status::Unavailable()`。
2. 创建 `src/client/cached_hash_ring.h/.cpp` — 本地 ring 缓存。
3. 创建 `src/client/master_client.h/.cpp` — Master stub 封装与 GetHashRing。
4. 创建 `src/client/worker_client.h/.cpp` — Worker stub 封装（ReadPages/WritePages 骨架）。
5. 创建 `src/client/fluxcache_client.h/.cpp` — 门面，整合 ChannelPool、MasterClient、CachedHashRing。
6. 更新 `src/client/CMakeLists.txt` — 添加新源文件，移除 stub.cpp 占位。
7. 创建 `tests/client/client_test.cpp` — 单元测试。
8. 创建 `tests/client/CMakeLists.txt`，在 `tests/CMakeLists.txt` 中 add_subdirectory(client)。
9. 执行 Testing Gate：构建、测试、lint。

## Risks & Assumptions

- **Risk**: CachedHashRing 与 HashRingManager 哈希逻辑需一致，否则路由结果不同。**Mitigation**: 复用相同 FNV1a 实现。
- **Assumption**: Phase 1 不实现 ring 自动刷新策略，由调用方在需要时调用 RefreshRing；FluxCacheClient::GetWorkerForBlock 内部可先调用 GetHashRing 获取最新 ring。
- **Assumption**: WorkerClient 按地址创建，每次 GetWorkerClient 可能创建新 WorkerClient 实例（轻量，仅持有 stub 指针）。

## To Confirm

- [x] 验收标准以 issues/P1-07-client-skeleton.md 为准。
- [x] 所有 RPC 设置 deadline。
- [x] 无 Worker、Master 不可达、Worker 不可达返回稳定错误码。

## 变更分级

P1 — 涉及 Client 公共接口、RPC 封装、Status 扩展。

## 测试设计

- **ChannelPool 创建 Master/Worker stub**: FluxCacheClient 初始化后，可成功创建 MasterClient 与 WorkerClient（通过 GetHashRing 或 GetWorkerClient 验证）。
- **Deadline 设置**: 对不可达地址发起 GetHashRing，验证在 deadline 内返回（mock 或真实超时）。
- **Ring 缓存初始化与刷新**: 构造 GetHashRingResponse，调用 CachedHashRing::Update，验证 GetVersion、GetWorker、GetWorkerAddress 正确。
- **无 Worker**: CachedHashRing 空或 GetWorker 返回 0，GetWorkerForBlock 返回 NotFound。
- **Master 不可达**: 对无效地址调用 GetHashRing，验证返回 Unavailable。
- **Worker 不可达**: 对无效 Worker 地址调用 ReadPages，验证返回 Unavailable。
