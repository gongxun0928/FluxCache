#!/usr/bin/env bash
# FluxCache S3 e2e test runner
# Runs inside Docker Compose test-runner container
#
# Prerequisites:
#   - fluxcache-master and fluxcache-worker are running
#   - MinIO is running with bucket pre-created
#
# Environment variables:
#   MASTER_HOST       — Master hostname (default: fluxcache-master)
#   MASTER_PORT       — Master port (default: 9090)
#   MINIO_ENDPOINT    — MinIO endpoint (default: minio:9000)
#   MINIO_BUCKET      — MinIO bucket (default: fluxcache-test)

set -euo pipefail

MASTER_HOST="${MASTER_HOST:-fluxcache-master}"
MASTER_PORT="${MASTER_PORT:-9090}"
MINIO_ENDPOINT="${MINIO_ENDPOINT:-minio:9000}"
MINIO_BUCKET="${MINIO_BUCKET:-fluxcache-test}"

CLI="fluxcache-cli --master ${MASTER_HOST} --port ${MASTER_PORT}"
PASS=0
FAIL=0
TOTAL=0

log()  { echo "[$(date '+%H:%M:%S')] $*"; }
pass() { PASS=$((PASS + 1)); TOTAL=$((TOTAL + 1)); log "PASS: $*"; }
fail() { FAIL=$((FAIL + 1)); TOTAL=$((TOTAL + 1)); log "FAIL: $*"; }

# ---- Wait for Master to be ready ----
log "Waiting for Master at ${MASTER_HOST}:${MASTER_PORT}..."
for i in $(seq 1 30); do
  if $CLI ls-mounts >/dev/null 2>&1; then
    log "Master is ready"
    break
  fi
  if [ "$i" -eq 30 ]; then
    log "FATAL: Master not ready after 30s"
    exit 1
  fi
  sleep 1
done

# ---- Wait for Worker to register ----
log "Waiting for Worker to register..."
for i in $(seq 1 30); do
  STAT_OUTPUT=$($CLI stat / 2>&1) || true
  if echo "${STAT_OUTPUT}" | grep -qi "ring_version\|worker"; then
    log "Worker registered"
    break
  fi
  if [ "$i" -eq 30 ]; then
    log "WARN: Worker not detected after 30s, proceeding anyway"
    break
  fi
  sleep 1
done

# ---- Test 1: Mount S3 bucket ----
log "Test 1: Mount S3 bucket"
UFS_URI="s3://${MINIO_ENDPOINT}/${MINIO_BUCKET}"
if $CLI mount /s3test "${UFS_URI}"; then
  pass "mount /s3test -> ${UFS_URI}"
else
  fail "mount /s3test"
fi

# ---- Test 2: List mounts ----
log "Test 2: List mounts"
if $CLI ls-mounts | grep -q "/s3test"; then
  pass "ls-mounts shows /s3test"
else
  fail "ls-mounts does not show /s3test"
fi

# ---- Test 3: Read file from S3 (pre-uploaded by minio-init) ----
log "Test 3: Read pre-existing file from S3 (hello.txt)"
READ_RESULT=$($CLI read /s3test/hello.txt 2>&1) || true
if echo "${READ_RESULT}" | grep -q "Hello from S3"; then
  pass "read /s3test/hello.txt = 'Hello from S3 e2e test!'"
else
  fail "read /s3test/hello.txt (got: ${READ_RESULT})"
fi

# ---- Test 4: Read nested file from S3 ----
log "Test 4: Read nested file from S3 (data/test.txt)"
READ_RESULT=$($CLI read /s3test/data/test.txt 2>&1) || true
if echo "${READ_RESULT}" | grep -q "FluxCache integration"; then
  pass "read /s3test/data/test.txt"
else
  fail "read /s3test/data/test.txt (got: ${READ_RESULT})"
fi

# ---- Test 5: Write new file to S3 ----
log "Test 5: Write new file to S3"
TEST_DATA="S3 write test $(date +%s)"
if $CLI write /s3test/write_test.txt "${TEST_DATA}"; then
  pass "write /s3test/write_test.txt"
else
  fail "write /s3test/write_test.txt"
fi

# ---- Test 6: Read back written file ----
log "Test 6: Read back written file"
sleep 1  # brief wait for consistency
READ_RESULT=$($CLI read /s3test/write_test.txt 2>&1) || true
if echo "${READ_RESULT}" | grep -q "S3 write test"; then
  pass "read back /s3test/write_test.txt matches written data"
else
  fail "read back /s3test/write_test.txt (got: ${READ_RESULT})"
fi

# ---- Test 7: Verify data in MinIO directly (via S3 API) ----
log "Test 7: Verify written data exists in MinIO"
MINIO_HTTP="http://${MINIO_ENDPOINT}"
VERIFY_HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
  -u "fluxcache:fluxcache123" \
  "${MINIO_HTTP}/${MINIO_BUCKET}/write_test.txt" 2>/dev/null || echo "000")
if [ "${VERIFY_HTTP_CODE}" = "200" ]; then
  pass "write_test.txt verified in MinIO via S3 API"
else
  fail "write_test.txt not found in MinIO (HTTP ${VERIFY_HTTP_CODE})"
fi

# ---- Test 8: Stat file ----
log "Test 8: Stat file"
STAT_RESULT=$($CLI stat /s3test/hello.txt 2>&1) || true
if echo "${STAT_RESULT}" | grep -qi "size\|inode\|mtime"; then
  pass "stat /s3test/hello.txt returns metadata"
else
  fail "stat /s3test/hello.txt (got: ${STAT_RESULT})"
fi

# ---- Test 9: Unmount ----
log "Test 9: Unmount"
if $CLI unmount /s3test; then
  pass "unmount /s3test"
else
  fail "unmount /s3test"
fi

# ---- Summary ----
echo ""
echo "========================================="
echo "  FluxCache S3 e2e Test Results"
echo "========================================="
echo "  Total: ${TOTAL}"
echo "  Passed: ${PASS}"
echo "  Failed: ${FAIL}"
echo "========================================="

if [ "${FAIL}" -gt 0 ]; then
  exit 1
fi
exit 0
