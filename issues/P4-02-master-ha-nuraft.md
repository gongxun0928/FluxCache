# P4-02: Master HA via NuRaft

## Summary

Introduce NuRaft to replicate Master metadata changes across a Raft group, enabling automatic leader election and failover. Followers serve read requests via ReadIndex/Lease Read.

## Current State

- Single Master, single point of failure
- HA Journal design spike completed (design/ha-journal-design.md)
- MountTable persistence (P4-01) is a prerequisite

## Acceptance Criteria

1. NuRaft library integrated via vcpkg or FetchContent
2. Raft state machine wraps InodeTree + MountTable mutations
3. Write RPCs (CreateFile, CompleteFile, DeleteFile, Mount, Unmount) go through Raft log
4. Follower can serve GetFileInfo / GetHashRing via ReadIndex
5. Leader election and failover work: data accessible after switchover
6. Configuration supports standalone (1-node) and cluster (3-node) modes
7. Write latency increase < 5ms for same-datacenter deployment

## Non-Goals

- Raft-based Worker topology persistence (Workers re-register)
- Cross-datacenter replication
- Snapshot transfer optimization (first version uses full RocksDB copy)

## Dependencies

- P4-01 (MountTable persistence)

## Change Tier

P0 (core data model, cross-service contract, production stability)
