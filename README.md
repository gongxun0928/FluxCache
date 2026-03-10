# FluxCache

[中文文档](./README.zh-CN.md)

FluxCache is a **C++20 distributed cache file system prototype** inspired by Alluxio and GooseFS.

The project is intentionally being built around a narrow MVP first:

- single `Master`
- single `Worker`
- `LocalFS` as the first UFS
- page cache in `Memory` first
- `write-through` writes
- `mtime`-based cache validation in Phase 1

## Status

> Current focus: freeze the Phase 1 contract and make the first end-to-end read/write path verifiable.

The repository should be read as:

- a stable **architecture direction**
- an actively evolving **implementation roadmap**
- not a claim that all planned features are already production ready

## Current Architecture Target

```text
Client
  |  gRPC
  v
Single Master (MountTable + InodeTree + WorkerManager + HashRing)
  |  gRPC
  v
Single Worker (Memory Tier + PageStore)
  |
  v
Under File System (LocalFS first)
```

### Core Responsibilities

- `Client`
  - asks Master for file metadata
  - computes `BlockId` locally
  - routes reads/writes to Worker
- `Master`
  - owns namespace and inode metadata
  - resolves mount paths
  - exposes worker list and ring version
- `Worker`
  - serves page reads/writes
  - caches pages locally
  - falls back to UFS on cache miss
- `UFS`
  - remains the durable source of truth

## Stable Design Decisions

- `InodeId` is the file identity.
- `BlockId = (InodeId, BlockIndex)` is the routing identity.
- `PageId = {BlockId, page_index}` is the cache identity.
- Master does **not** store `Block -> Worker` locations in Phase 1.
- Worker does **not** own file path metadata.
- Cache freshness is checked with `ufs_mtime_ms` in Phase 1.

## Read Path (Phase 1)

1. Client calls `GetFileInfo`.
2. Master returns `inode_id`, `size`, `block_size`, `ufs_mtime_ms`, `ring_version`, `workers`, `ufs_uri`, and `ufs_path`.
3. Client computes `BlockId` and `page_indices` locally.
4. Client sends `ReadPages` to Worker.
5. Worker checks `PageStore`.
6. On miss or stale cache, Worker reads from UFS and refills cache.

## Write Path (Phase 1)

1. Client calls `CreateFile` or `GetFileInfo`.
2. Client computes target blocks/pages locally.
3. Client sends `WritePages` to Worker.
4. Worker writes to UFS first.
5. Worker updates cache only after UFS write succeeds.
6. Client calls `CompleteFile` so Master updates metadata.

## Roadmap

The authoritative roadmap lives in [issues/index.md](./issues/index.md).

High-level phases:

- `Phase A`: contract freeze
- `Phase B`: metadata-plane closure
- `Phase C`: data-plane closure
- `Phase D`: recovery and deletion semantics
- `Phase E`: multi-worker and transport hardening
- `Phase F`: multi-tier cache and eviction
- `Phase G`: access surfaces (`CLI`, `SDK`, `FUSE`)
- `Phase H`: backend expansion (`S3`, `HDFS`)
- `Phase I`: HA and resilience
- `Phase J`: performance, observability, and quality track

### Planned, Not Core Yet

These are still roadmap items, not current stable core claims:

- SSD/HDD tiers
- MetaStore-based worker recovery
- FUSE access
- C++ SDK beyond file-path MVP
- S3 / HDFS backends
- HA journal / Raft
- advanced resilience and degradation
- production observability

## Build and Test

### Requirements

- C++20 compiler
- CMake 3.20+
- Protobuf + gRPC
- RocksDB
- yaml-cpp

Optional later-stage dependencies:

- FUSE3
- AWS SDK / S3 client stack
- HDFS client libraries
- Prometheus C++ client

### Build

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

### Test

```bash
cd build
ctest --output-on-failure
```

## Contributing

When changing the roadmap or implementation, keep these files consistent:

- `README.md`
- `README.zh-CN.md`
- `issues/index.md`
- `plan/issue-status.md`
- relevant docs in `design/`

## License

[MIT License](./LICENSE)
# FluxCache

[中文文档 (Chinese Version)](./README.zh-CN.md)

FluxCache is a **C++20 distributed cache file system** inspired by the architecture of [Alluxio](https://www.alluxio.io/) and GooseFS.  
It provides a **unified namespace**, **multi-tier caching**, and **high-throughput RPC-based data access** over heterogeneous storage backends.

> Status: Architecture and interfaces are being actively built.

## Audience and Navigation

### For Users (Operators / Data Platform Teams)

Focus on these sections first:

- [Core Features](#core-features)
- [RPC and Timeout Model](#rpc-and-timeout-model)
- [Data Flow (Read Path)](#data-flow-read-path)
- [Data Flow (Write Path, Write-through)](#data-flow-write-path-write-through)
- [Configuration (Planned)](#configuration-planned)

User-facing value:

- one logical namespace over mixed storage;
- transparent caching acceleration for hot data;
- POSIX-compatible access via FUSE plus SDK/CLI options.

### For Developers (Contributors / Integrators)

Focus on these sections first:

- [Architecture](#architecture)
- [Build and Test (Developer Guide)](#build-and-test-developer-guide)
- [Roadmap (High Level)](#roadmap-high-level)
- [Design Goals and Non-goals](#design-goals-and-non-goals)
- [Contributing](#contributing)

## Why FluxCache

Modern data platforms often mix local disks, object storage, and distributed filesystems. Application access patterns are also diverse: small random reads, large sequential scans, and bursty hot datasets.

FluxCache is designed to:

- expose one logical namespace across many storage systems;
- accelerate hot data with hierarchical caching (Memory -> SSD -> HDD);
- preserve compatibility through POSIX-like mounting and SDK/CLI access;
- remain operationally simple with gRPC, observability, and metadata persistence.

## Architecture

```text
Client (FUSE / SDK / CLI)
        |  gRPC
        v
Master Cluster (Metadata + Namespace)
        |  gRPC
        v
Worker Pool (Multi-tier Cache: Memory -> SSD -> HDD)
        |
        v
Under File System (Local FS / S3 / HDFS)
```

### Component Responsibilities

- **Client**
  - FUSE mount for POSIX workflows;
  - SDK/CLI for programmatic and operational control;
  - gRPC `ChannelPool` to reuse HTTP/2 connections and reduce connect overhead.
- **Master Cluster**
  - Global metadata and namespace management;
  - mount table management;
  - HA-ready journal replication skeleton based on Raft.
- **Worker Pool**
  - data plane for read/write and cache serving;
  - tier-aware placement, promotion, and eviction;
  - page-level metadata persistence for fast restart.
- **Under File System (UFS)**
  - pluggable backend abstraction (Local FS, S3, HDFS);
  - source of truth for durable objects/files.

## Core Features

- **Unified Namespace (`MountTable`)**
  - mount multiple UFS roots to one logical path tree.
- **Multi-tier Cache (`StorageTier`)**
  - Memory / SSD / HDD tiers with automatic promotion and eviction.
- **Page-level Cache (`PageStore`)**
  - 1 MB page granularity to optimize random small I/O.
- **Persistent Worker Metadata (`MetaStore`)**
  - RocksDB-backed cache index recovery after worker restart.
- **Pluggable Eviction Policies**
  - LRU and LFU.
- **End-to-end gRPC Communication**
  - full RPC pipeline with connection pooling and configurable timeouts.
- **FUSE Integration**
  - POSIX-compatible mount for standard filesystem tooling.
- **S3 Backend Support**
  - AWS S3-compatible object storage.
- **HDFS Backend Support (Planned)**
  - Hadoop-compatible distributed filesystem via HDFS/WebHDFS.
- **Automatic Tier Management**
  - hot data promotion and capacity-driven eviction.
- **Master HA Skeleton**
  - Raft journal replication framework (scaffolding).
- **Observability**
  - Prometheus-compatible `/metrics` endpoint.

## RPC and Timeout Model

All component interactions use gRPC.  
Every RPC call should be configured with explicit deadlines/timeouts to avoid I/O thread starvation under slow backend or network jitter.

Key principles:

- connection reuse first (`ChannelPool`);
- bounded latency per call (deadline budget);
- fail-fast and retry only where operation semantics are safe.

## Data Flow (Read Path)

1. Client queries Master for file metadata and Worker location via gRPC.
2. Client sends data read request directly to the target Worker.
3. Worker checks `PageStore` in tier priority order (Memory -> SSD -> HDD).
4. On cache miss, Worker fetches from UFS.
5. Data is split/served at page granularity and inserted into cache tiers.
6. Hot pages are promoted, cold pages are evicted per policy.

## Data Flow (Write Path, Write-through)

1. Client queries Master for metadata, then sends write request to the target Worker.
2. Worker persists data to UFS synchronously (write-through).
3. Worker updates cached pages and metadata index.
4. Master metadata reflects namespace/block/page mapping updates.

## Build and Test (Developer Guide)

> The current repository is in early bootstrap stage. The commands below are the recommended baseline workflow.

### Requirements

- C++20 compiler:
  - GCC 11+ or Clang 14+ (recommended);
- CMake 3.20+;
- Protobuf + gRPC;
- RocksDB;
- FUSE3 development headers (for FUSE client);
- OpenSSL (typically required by gRPC/S3 stacks);
- Prometheus C++ client library (or equivalent metrics exporter).

### Build Steps

```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j
```

### Test (recommended quality gate)

```bash
cd build
ctest --output-on-failure
```

## Configuration (Planned)

- `master.*`: metadata service endpoints, election/journal settings;
- `worker.*`: tier capacities, page size, eviction policy;
- `client.*`: RPC timeout, retry budget, channel pool size;
- `ufs.*`: backend type and credentials (S3/HDFS/local).

## Roadmap (High Level)

- [ ] Namespace and mount-table lifecycle APIs;
- [ ] Worker page-cache engine with tiered placement;
- [ ] End-to-end read/write protocol and error model;
- [ ] CLI and SDK client interfaces;
- [ ] Master HA journal replication completion;
- [ ] HDFS backend driver;
- [ ] Production-grade observability and profiling.

## Design Goals and Non-goals

### Goals

- predictable latency under mixed workloads;
- clear separation of metadata plane and data plane;
- backend-agnostic access through unified namespace.

### Non-goals (Current Stage)

- replacing long-term durable storage;
- custom application-level consistency semantics outside filesystem scope.

## Contributing

Contributions are welcome.  
When proposing changes, include:

- motivation and expected behavior;
- architecture impact (Master/Worker/Client/UFS);
- test plan and risk notes.

## License

This project is licensed under the [MIT License](./LICENSE).

