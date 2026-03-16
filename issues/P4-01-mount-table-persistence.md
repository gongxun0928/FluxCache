# P4-01: MountTable RocksDB Persistence

## Summary

Persist MountTable to RocksDB so mount points survive Master restart. This is a prerequisite for Master HA (P4-02).

## Current State

- `MountTable` stores mounts in `std::map<std::string, std::string>` (in-memory only)
- `InodeStore` already has RocksDB with `inodes` and `edges` column families
- `create_missing_column_families = true` is set, so adding a new CF is backward-compatible

## Acceptance Criteria

1. `InodeStore` exposes `PutMount` / `DeleteMount` / `IterateMounts` using a new `mounts` CF
2. `MountTable` accepts an optional `InodeStore*` binding; when bound, Mount/Unmount writes to RocksDB
3. `MasterServer::Start()` recovers MountTable from RocksDB after InodeTree recovery
4. Existing tests pass without modification
5. New test: Master restart with pre-existing mount points — mount info is preserved

## Non-Goals

- Raft replication of mount changes (deferred to P4-02)
- MountTable versioning or conflict resolution

## Dependencies

- None (builds on existing InodeStore / RocksDB infrastructure)

## Change Tier

P1 (single-service core logic, persistent state change)
