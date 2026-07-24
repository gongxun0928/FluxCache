# P5-04: Docker-Compose Integration Test Environment

## Summary

Create a Docker Compose integration test environment that launches MinIO (S3-compatible backend) + FluxCache master/worker containers, enabling one-command S3 e2e test execution.

## Current State

- **Status (2026-07-23):** `completed` via PR #2 (infra landed)
- `docker/Dockerfile`, `docker/docker-compose.yml`, `docker/run_e2e_test.sh`, `docker/config/config.yaml` exist
- Services: minio, minio-init, fluxcache-master, fluxcache-worker, test-runner
- Re-run recommendation when Docker is available:
  `cd docker && docker compose up --build && docker compose run test-runner`

## Acceptance Criteria

1. `docker/Dockerfile` — multi-stage build: builder (vcpkg + cmake) → runtime (minimal Ubuntu + binaries)
2. `docker/docker-compose.yml` with services:
   - `minio` — S3-compatible storage with healthcheck
   - `minio-init` — one-shot: create bucket + upload test files
   - `fluxcache-master` — built from local source
   - `fluxcache-worker` — built from local source
   - `test-runner` — runs S3 e2e test suite
3. `docker/run_e2e_test.sh` — 9 test cases covering:
   - Mount S3 bucket
   - List mounts
   - Read pre-existing file from S3
   - Read nested file from S3
   - Write new file to S3
   - Read back written file
   - Verify data in MinIO directly
   - Stat file metadata
   - Unmount
4. `docker compose up --build && docker compose run test-runner` passes
5. `docker compose down -v` cleans up all state

## Non-Goals

- CI pipeline integration (separate issue)
- Performance testing under Docker
- FUSE mount inside Docker (requires --privileged or fuse device)

## Dependencies

- None (uses existing binaries and S3UFS implementation)

## Change Tier

P1 (test infrastructure, no production code change)
