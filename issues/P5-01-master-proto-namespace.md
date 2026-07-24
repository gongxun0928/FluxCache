# P5-01: Master Proto Namespace RPC Extension

## Summary

Expose existing InodeTree directory operations as Master RPC endpoints. Add Mkdir/Rmdir/ListDir/Stat RPCs (wire-up) and implement Rename RPC (new logic).

## Current State

- **Status (2026-07-23):** `completed` via PR #2
- Master exposes `Mkdir` / `Rmdir` / `ListDir` / `Rename` / `Stat`
- `InodeTree::RenameInode()` covers same-dir, cross-dir, and overwrite cases
- Client wrappers and master unit tests landed with the same PR

## Acceptance Criteria

### Proto (master.proto)

1. Add RPCs: `Mkdir`, `Rmdir`, `ListDirectory`, `Rename`, `GetInodeStat`
2. Add request/response messages for each RPC
3. Add `RenameOp` to JournalEntry (for Raft HA future use)

### MasterServiceImpl

4. `Mkdir` — calls `InodeTree::CreateDirectory()`
5. `Rmdir` — calls `InodeTree::DeleteInode()` (empty dir only)
6. `ListDirectory` — calls `InodeTree::ListDirectory()` → returns name + inode_id pairs
7. `Rename` — new `InodeTree::RenameInode()` implementation:
   - Same-directory rename: change child name
   - Cross-directory rename: remove from src dir children, add to dst dir children
   - Overwrite target: delete target inode first, then move
   - Atomic under write lock
8. `GetInodeStat` — calls `InodeTree::GetInode()`

### MasterClient

9. Add client-side wrappers for all 5 new RPCs

### Tests

10. Unit tests for each new RPC
11. Rename edge cases: same-dir rename, cross-dir rename, overwrite target, non-existent src, non-empty dir rmdir

## Non-Goals

- Chmod/Chown/Truncate/Utimens (Phase 2, separate issue)
- Link/Symlink/XAttr (Phase 3, separate issue)
- Raft replication for Rename (future)

## Dependencies

- None

## Change Tier

P1
