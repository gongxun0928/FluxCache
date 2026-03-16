# P4-03: Multi-Worker Deployment Test

## Summary

Provide a shell-based deployment test script that starts a real Master + multiple Worker processes, runs end-to-end validation, and reports results. Complements the in-process integration test (multi_worker_test.cpp).

## Current State

- In-process multi-worker integration test exists and passes (8 test cases)
- No multi-process deployment test script exists
- Master, Worker binaries exist as `fluxcache-master` and `fluxcache-worker`

## Acceptance Criteria

1. Shell script starts 1 Master + 2 Workers as separate processes
2. Uses CLI tool or SDK example to Mount, write files, read files
3. Validates read content matches written content
4. Tests Worker shutdown + read fallback
5. Cleans up all processes and temp dirs on exit (trap)
6. Exit code 0 on success, non-zero on failure

## Non-Goals

- Cross-machine deployment (single-machine multi-process is sufficient)
- Performance benchmarking (covered by existing bench tests)

## Dependencies

- None (uses existing binaries)

## Change Tier

P2 (test infrastructure, no production code change)
