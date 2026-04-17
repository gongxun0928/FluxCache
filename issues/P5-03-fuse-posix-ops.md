# P5-03: FUSE Complete POSIX Operations

## Summary

Implement full POSIX file system operations in the FUSE layer by calling the C++ SDK. Currently only basic file I/O works; directory and attribute operations return ENOTSUP.

## Current State

- FUSE uses high-level API (`fuse_operations` struct, FUSE_USE_VERSION 31)
- Implemented: getattr, open, release, read, write, create
- All returning ENOTSUP: readdir, mkdir, rmdir, unlink, rename, truncate, chmod, chown, utimens, symlink, link, readlink, setxattr, getxattr, listxattr, removexattr

## Acceptance Criteria

### Batch 1 — Core directory ops (P1)
1. `fc_readdir` — call SDK ListDirectory, use fuse filler
2. `fc_mkdir` — call SDK Mkdir
3. `fc_rmdir` — call SDK Rmdir
4. `fc_unlink` — call SDK Delete
5. `fc_rename` — call SDK Rename

### Batch 2 — Attribute ops (P1)
6. `fc_truncate` — implement file truncation
7. `fc_chmod` — update file permissions (if Master supports)
8. `fc_chown` — update file ownership (if Master supports)
9. `fc_utimens` — update file timestamps

### Batch 3 — Advanced (P2)
10. `fc_symlink` / `fc_readlink` — symbolic link support
11. `fc_link` — hard link support (if scope includes)
12. XAttr operations (if scope includes)

### Testing
13. FUSE mount point usable with standard shell commands: `ls`, `mkdir`, `cp`, `mv`, `rm`
14. Integration test: mount → shell operations → verify

## Scope Decisions

- Hard links (link): NOT in scope
- File locks (flock/lockf): NOT in scope
- atime: approximate (guarantee mtime/ctime)

## Dependencies

- P5-02 (SDK complete namespace operations)

## Change Tier

P1
