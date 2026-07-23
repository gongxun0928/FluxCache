# FluxCache

[中文文档](./README.zh-CN.md)

FluxCache is a **C++20 distributed cache file system prototype** inspired by Alluxio and GooseFS.

The project grew from a narrow MVP and now covers most of Phase A–K:

- single `Master` (RocksDB-backed metadata; HA Raft suspended)
- one or more `Worker`s with Memory / SSD / HDD tiers
- UFS backends: `LocalFS` (first), `S3/MinIO` (required), `HDFS` (stub / optional)
- page cache with write-through and optional write-back eviction paths
- cache validation via Master-maintained `file_version` (replaced earlier `mtime` checks)

## Status

> Current focus: close Phase K residuals (pjdfstest baseline), then choose the next direction — harden the current architecture, or freeze a larger replan (see open draft PR #1).

Issue status truth source: [plan/issue-status.md](./plan/issue-status.md). Active batch: [plan/active-batch.md](./plan/active-batch.md).

The repository should be read as:

- a stable **architecture direction** with a largely implemented roadmap through Phase J / most of Phase K
- not a claim that the system is production-hardened (HA paused; POSIX attrs / full truncate still limited)

## Current Architecture Target

```text
Client (CLI / SDK / optional FUSE)
  |  gRPC
  v
Single Master (MountTable + InodeTree + WorkerManager + HashRing)
  |  gRPC
  v
Worker(s) (Memory/SSD/HDD Tier + PageStore + MetaStore)
  |
  v
Under File System (LocalFS / S3 / HDFS stub)
```

### Core Responsibilities

- `Client`
  - asks Master for file metadata and namespace ops
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
- Master does **not** store `Block -> Worker` locations; Client routes via HashRing.
- Worker does **not** own file path metadata.
- Cache freshness is checked with Master `file_version` (incremented on CompleteFile / writes).

## Read Path

1. Client calls `GetFileInfo`.
2. Master returns `inode_id`, `size`, `block_size`, `file_version`, `ring_version`, `workers`, `ufs_uri`, and `ufs_path`.
3. Client computes `BlockId` and `page_indices` locally.
4. Client sends `ReadPages` to Worker with `expected_file_version`.
5. Worker checks `PageStore`.
6. On miss or version mismatch, Worker reads from UFS and refills cache.

## Write Path

1. Client calls `CreateFile` or `GetFileInfo`.
2. Client computes target blocks/pages locally.
3. Client sends `WritePages` to Worker.
4. Worker writes to UFS first (write-through default).
5. Worker updates cache only after UFS write succeeds.
6. Client calls `CompleteFile` so Master updates size and increments `file_version`.

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
- `Phase K`: production-readiness extras (namespace RPCs, Docker e2e, pjdfstest)

### Still open or limited

- **P5-05** pjdfstest: scripts + expected baseline docs exist; measured report and Compose integration pending
- FUSE attribute persistence / full truncate (known POSIX gaps)
- HDFS UFS real driver (stub unless libhdfs enabled)
- Master HA / Raft (`P4-02` suspended; `FLUXCACHE_ENABLE_RAFT=OFF` by default)
- Draft five-layer architecture replan (open PR #1; not merged)

## Build and Test

### Requirements

- C++20 compiler
- CMake 3.20+
- Protobuf + gRPC
- RocksDB
- yaml-cpp
- **S3/MinIO**: vcpkg + minio-cpp (required)

Optional later-stage dependencies:

- FUSE3
- HDFS client libraries
- Prometheus C++ client

### Build

S3 UFS (minio-cpp) is required. The simplest way is to use the build script:

```bash
./build.sh
```

This uses the project's `vcpkg/` (or `VCPKG_ROOT` if set), bootstraps vcpkg if needed, and builds. Optional: `./build.sh [build_dir] [cmake_args...]`, e.g. `./build.sh build -DFLUXCACHE_ENABLE_FUSE=ON`.

Or manually with vcpkg:

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build .
# For IDE: ln -sf "$(pwd)/compile_commands.json" ../compile_commands.json
```

**IDE IntelliSense (clangd / C++ extension):** Run `./build.sh` once (or create the symlink above after manual cmake). This lets Cursor/VSCode resolve includes like `worker/page/page_store.h` and jump to standard library headers.

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
