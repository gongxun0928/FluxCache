# P4-02: Master HA via NuRaft

> Status: **suspended** — default build no longer includes Raft; revisit after production single-master stabilisation.

## Summary

Introduce NuRaft to replicate Master metadata changes across a Raft group, enabling automatic leader election and failover.

## Current State

- Single Master, single point of failure.
- HA Journal design spike completed (design/ha-journal-design.md).
- MountTable persistence (P4-01) is a prerequisite — done.
- NuRaft prototype code exists behind `FLUXCACHE_ENABLE_RAFT` (default **OFF**).

## Decision: Suspend and Reassess

Master failover is a **high-risk, high-complexity** operation. The current prototype exposed several implementation-level problems (see below). Rather than shipping a fragile HA layer, we choose to:

1. **Default to single-master with standalone RocksDB.** This is the stable, well-tested path.
2. **Treat master switch as an offline, operator-driven action.** If the master is lost, a new master starts from the same RocksDB data directory (or a restored backup). Cache metadata loss is treated as a cold-start (no warm cache).
3. **Keep the Raft prototype code** gated behind `FLUXCACHE_ENABLE_RAFT=OFF` for future exploration.
4. **Target raft-based RocksDB** as the long-term direction (billion-file scale rules out etcd or similar external KV stores).

## Known Issues in Current NuRaft Prototype

### 1. PersistentLogStore O(N) full rewrite per append

`PersistAll()` serialises **every** uncommitted log entry into a new generation directory on each append. This is O(N) write amplification per operation and will degrade severely as the log grows between compactions.

**Impact:** Write latency linearly proportional to log depth; disk I/O storm under load.

### 2. Snapshot transfer is fragile

- Snapshot uses file-by-file `read_logical_snp_obj` / `save_logical_snp_obj`.
- `save_logical_snp_obj` returns `void` — failures during receive are silently swallowed.
- Large RocksDB checkpoints (many SST files) make this transfer slow and error-prone.

**Impact:** Follower catchup may fail silently; cluster recovery after snapshot transfer errors is undefined.

### 3. Passive health tracking without recovery

`healthy_` flag is set to `false` on I/O errors but nothing triggers recovery or alerts. The node continues to serve stale data or silently drops writes.

**Impact:** Silent data loss or divergence between Raft state and RocksDB state.

### 4. NuRaft ecosystem maturity

NuRaft (eBay) has limited community adoption compared to etcd-raft or braft. Debugging production issues is harder due to smaller knowledge base.

### 5. State machine complexity

The state machine handles 8+ operation types (CreateFile, CompleteFile, DeleteFile, Mount, Unmount, CreateDirectory, UpsertWorker, UpdateWorkerState). Each must be applied idempotently to InodeTree + MountTable. A bug in any apply path can cause Leader/Follower divergence.

## Future Direction

When revisiting Master HA:

- Consider **braft** or **openraft** as more mature embedded Raft libraries.
- Implement incremental log append (WAL-style) instead of full rewrite.
- Use streaming gRPC for snapshot transfer.
- Add health-check integration that triggers step-down or alerts on I/O failure.
- Explore offline RocksDB checkpoint shipping as an interim HA mechanism (simpler than full Raft).

## Acceptance Criteria (original, deferred)

1. ~~NuRaft library integrated via vcpkg or FetchContent~~
2. ~~Raft state machine wraps InodeTree + MountTable mutations~~
3. ~~Write RPCs go through Raft log~~
4. ~~Follower can serve reads from local replicated state~~
5. ~~Leader election and failover work~~
6. ~~Configuration supports standalone (1-node) and cluster (3-node) modes~~
7. ~~Write latency increase < 5ms for same-datacenter deployment~~

## Dependencies

- P4-01 (MountTable persistence) — completed

## Change Tier

P0 (core data model, cross-service contract, production stability)
