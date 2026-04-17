# Rename dst parent sync design

> Status: Done

## Summary

`MasterServiceImpl::Rename` currently synchronizes only `Dirname(src_path)` from UFS before
calling `InodeTree::RenameInode`. For cross-directory renames, `RenameInode` resolves both
source and destination parent paths from `InodeTree`. If the destination parent was never
synced into cache, the rename fails even when the destination directory exists in UFS.

## Decision

Before invoking `RenameInode`, synchronize both:

1. `Dirname(src_path)`
2. `Dirname(dst_path)`

This keeps `Rename` aligned with the existing parent-sync pattern already used by
other mutating RPCs (`CreateFile`, `Mkdir`) and ensures the destination parent path is
materialized in `InodeTree`.

## Scope

- Update `src/master/master_service_impl.cpp`
- Add a regression test in `tests/master/sync_from_ufs_test.cpp`

## Risks

- Extra `SyncFromUfs` call on the destination parent increases rename preflight work.
- Same-directory renames will sync the same parent twice unless explicitly deduplicated.

## Chosen tradeoff

Prefer minimal, behavior-preserving code: issue the destination sync unconditionally after
the source sync. This mirrors existing RPC patterns and avoids broad refactoring.

## Validation

- Add a regression test that keeps `/mnt/src/file.txt` materialized in `InodeTree`
  while leaving `/mnt/dst` discoverable only from UFS.
- Call `MasterServiceImpl::Rename("/mnt/src/file.txt", "/mnt/dst/file.txt")`.
- Verify rename succeeds and the inode moves under the destination parent.
