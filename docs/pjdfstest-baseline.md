# pjdfstest POSIX Compatibility Baseline — FluxCache

## Overview

This document maps FluxCache's FUSE implementation to expected pjdfstest results.
FluxCache is a **distributed cache filesystem**, not a full POSIX filesystem, so
many pjdfstest categories will intentionally fail.

## Expected Results by Category

### 1. File I/O Operations (open/read/write/create)
- **Expected**: Most tests PASS
- FluxCache implements: `fc_open`, `fc_read`, `fc_write`, `fc_create`, `fc_release`
- **Potential failures**:
  - O_APPEND semantics — may not be fully supported
  - O_TRUNC on open — depends on truncate implementation
  - Large file tests — depends on block/page configuration

### 2. Directory Operations (mkdir/rmdir/readdir)
- **Expected**: Most tests PASS
- FluxCache implements: `fc_mkdir`, `fc_rmdir`, `fc_readdir`
- **Potential failures**:
  - Deeply nested directory creation — depends on path resolution depth
  - Concurrent directory operations — single Master lock

### 3. File Deletion (unlink)
- **Expected**: Most tests PASS
- FluxCache implements: `fc_unlink` (delegates to SDK Delete)
- **Potential failures**:
  - unlink of open file — depends on InodeTree semantics
  - unlink of read-only file — permission not enforced

### 4. Rename
- **Expected**: Most tests PASS
- FluxCache implements: `fc_rename` with RENAME_NOREPLACE support
- **Expected failures**:
  - `RENAME_EXCHANGE` — returns ENOTSUP
  - rename hard-linked files — hard links not supported

### 5. Hard Links (link)
- **Expected**: ALL FAIL (ENOTSUP)
- FluxCache does not implement `fc_link`
- This is a design decision: cache filesystems typically don't support hard links

### 6. Symbolic Links (symlink/readlink)
- **Expected**: ALL FAIL (ENOTSUP)
- FluxCache does not implement `fc_symlink` or `fc_readlink`

### 7. Permissions (chmod)
- **Expected**: Tests may PASS (no-op returns 0) or FAIL (changes not persisted)
- FluxCache `fc_chmod` returns 0 (silently accepts) but doesn't persist changes
- Tests that verify persistence across stat calls will FAIL

### 8. Ownership (chown)
- **Expected**: Similar to chmod — no-op, not persisted
- Tests requiring root will be skipped if not run as root

### 9. Timestamps (utimens)
- **Expected**: Tests may PASS (no-op) or FAIL (timestamps not persisted)
- FluxCache `fc_utimens` returns 0 but doesn't update timestamps
- atime is not tracked at all

### 10. Extended Attributes (xattr)
- **Expected**: ALL FAIL (ENOTSUP)
- FluxCache does not implement xattr operations

### 11. Truncate
- **Expected**: Partial PASS
- `truncate(path, 0)` — PASS (verifies file exists then returns 0)
- `truncate(path, N)` where N > 0 — FAIL (ENOTSUP)
- `ftruncate` via open file — depends on FileHandle support

### 12. Special Files (fifo/socket/block/char)
- **Expected**: ALL FAIL
- FluxCache only supports regular files and directories
- `mknod` for special files will fail

### 13. File Locking (flock/lockf)
- **Expected**: NOT TESTED by pjdfstest (typically)
- If tested, would FAIL — FluxCache doesn't implement file locking

## Estimated Pass Rate

| Category | Test Count (est.) | Expected Pass |
|----------|------------------|---------------|
| File I/O | ~80 | ~70 (87%) |
| Directory ops | ~30 | ~25 (83%) |
| Unlink | ~15 | ~12 (80%) |
| Rename | ~20 | ~15 (75%) |
| Hard links | ~20 | 0 (0%) |
| Symlinks | ~15 | 0 (0%) |
| Permissions | ~25 | ~5 (20%) |
| Ownership | ~15 | ~5 (33%) |
| Timestamps | ~15 | ~5 (33%) |
| xattr | ~10 | 0 (0%) |
| Truncate | ~10 | ~5 (50%) |
| Special files | ~5 | 0 (0%) |
| **Total** | **~260** | **~142 (55%)** |

## How to Run

```bash
# 1. Install pjdfstest
git clone https://github.com/pjd/pjdfstest.git /opt/pjdfstest
cd /opt/pjdfstest && make

# 2. Start FluxCache and mount FUSE
fluxcache-master --config config.yaml &
fluxcache-worker --config config.yaml &
sleep 2
fluxcache-fuse -o master=127.0.0.1 -o port=9090 /tmp/fc-fuse &
sleep 2

# 3. Mount a UFS path
fluxcache-cli mount /mnt/local /tmp/ufs_data

# 4. Run pjdfstest
./scripts/run_pjdfstest.sh /tmp/fc-fuse /opt/pjdfstest

# 5. Check report
cat build/pjdfstest-reports/summary_*.txt
```

## Improving Pass Rate

Priority order for improving POSIX compatibility:

1. **Persist chmod/chown/utimens** in InodeEntry + RocksDB — big impact, moderate effort
2. **Implement full truncate** — moderate effort
3. **Symlink support** — high effort, lower priority for cache FS
4. **Hard link support** — high effort, questionable value for cache FS
5. **xattr support** — moderate effort, depends on use cases
