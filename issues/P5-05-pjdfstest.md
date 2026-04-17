# P5-05: pjdfstest POSIX Compatibility Baseline

## Summary

Integrate the pjdfstest test suite against FluxCache's FUSE mount point to measure POSIX compatibility pass rate and identify gaps.

## Current State

- No POSIX compliance testing exists
- FUSE mount point supports basic file I/O (Phase K will add full ops)
- pjdfstest (https://github.com/pjd/pjdfstest) is a POSIX filesystem test suite

## Acceptance Criteria

1. pjdfstest integrated into Docker Compose environment
2. Runs against FluxCache FUSE mount point
3. Outputs pass/fail report with:
   - Total tests run
   - Pass count and rate
   - Failed test names grouped by category
4. Known limitations documented:
   - Hard links → ENOTSUP
   - Special files (fifo, socket, block/char device) → ENOTSUP
   - File locks → ENOTSUP
   - atime precision → approximate

## Non-Goals

- 100% POSIX compliance (cache filesystem has inherent limitations)
- Fixing all failing tests (separate issues per category)

## Dependencies

- P5-03 (FUSE complete POSIX operations)
- P5-04 (Docker-Compose integration environment)

## Change Tier

P2
