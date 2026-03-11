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
- **S3/MinIO**: vcpkg + minio-cpp (when `FLUXCACHE_ENABLE_S3=ON`)
- HDFS client libraries
- Prometheus C++ client

### Build

**Default (no S3):**

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

**With S3/MinIO UFS (vcpkg + minio-cpp):**

```bash
# Install vcpkg, then:
mkdir -p build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake -DFLUXCACHE_ENABLE_S3=ON
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
