#!/usr/bin/env bash
# run_pjdfstest.sh — Run pjdfstest POSIX compliance tests against FluxCache FUSE mount.
#
# Prerequisites:
#   1. pjdfstest cloned and built: https://github.com/pjd/pjdfstest
#   2. FluxCache master + worker running
#   3. FUSE mount active at MOUNT_POINT
#   4. Perl + prove (Test::Harness) installed
#
# Usage:
#   ./scripts/run_pjdfstest.sh [MOUNT_POINT] [PJDFSTEST_DIR]
#
# Example:
#   ./scripts/run_pjdfstest.sh /tmp/fc-fuse /opt/pjdfstest
#
# Environment:
#   MOUNT_POINT    — FUSE mount point (default: /tmp/fc-fuse)
#   PJDFSTEST_DIR  — pjdfstest repo directory (default: /opt/pjdfstest)

set -euo pipefail

MOUNT_POINT="${1:-${MOUNT_POINT:-/tmp/fc-fuse}}"
PJDFSTEST_DIR="${2:-${PJDFSTEST_DIR:-/opt/pjdfstest}}"
REPORT_DIR="$(dirname "$0")/../build/pjdfstest-reports"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"

echo "=== FluxCache pjdfstest POSIX Compliance Baseline ==="
echo "Mount point: ${MOUNT_POINT}"
echo "pjdfstest:   ${PJDFSTEST_DIR}"
echo ""

# Validate prerequisites
if ! command -v prove &>/dev/null; then
  echo "ERROR: 'prove' not found. Install perl Test::Harness."
  exit 1
fi

if [ ! -d "${PJDFSTEST_DIR}" ]; then
  echo "ERROR: pjdfstest directory not found: ${PJDFSTEST_DIR}"
  echo "Clone with: git clone https://github.com/pjd/pjdfstest.git ${PJDFSTEST_DIR}"
  exit 1
fi

if [ ! -d "${MOUNT_POINT}" ]; then
  echo "ERROR: Mount point not found: ${MOUNT_POINT}"
  echo "Start FluxCache FUSE first: fluxcache-fuse -o master=127.0.0.1 -o port=9090 ${MOUNT_POINT}"
  exit 1
fi

# Create test directory on mount
TEST_DIR="${MOUNT_POINT}/pjdfstest_$$"
mkdir -p "${TEST_DIR}"
echo "Test directory: ${TEST_DIR}"

# Prepare report directory
mkdir -p "${REPORT_DIR}"
REPORT_FILE="${REPORT_DIR}/report_${TIMESTAMP}.txt"
SUMMARY_FILE="${REPORT_DIR}/summary_${TIMESTAMP}.txt"

echo "Running pjdfstest..."
echo ""

# Run pjdfstest from test directory
cd "${TEST_DIR}"
prove -rv "${PJDFSTEST_DIR}" 2>&1 | tee "${REPORT_FILE}"
PROVE_EXIT=$?

cd - >/dev/null

# Generate summary
PASSED=$(grep -cE '^ok ' "${REPORT_FILE}" 2>/dev/null || echo "0")
FAILED=$(grep -cE '^not ok' "${REPORT_FILE}" 2>/dev/null || echo "0")
TOTAL=$((PASSED + FAILED))

cat > "${SUMMARY_FILE}" <<SUMMARY
FluxCache pjdfstest POSIX Compliance Report
============================================
Date:        $(date)
Mount point: ${MOUNT_POINT}
pjdfstest:   ${PJDFSTEST_DIR}
Git branch:  $(cd "$(dirname "$0")/.." && git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
Git commit:  $(cd "$(dirname "$0")/.." && git rev-parse --short HEAD 2>/dev/null || echo "unknown")

Results
------
Total tests:  ${TOTAL}
Passed:       ${PASSED}
Failed:       ${FAILED}
Pass rate:    $(awk "BEGIN {if (${TOTAL}>0) printf \"%.1f%%\", ${PASSED}*100/${TOTAL}; else print \"N/A\"}")

Known Limitations (expected failures)
------------------------------------
FluxCache is a distributed cache filesystem, not a full POSIX filesystem.
The following categories are expected to have failures:

1. Hard links (link) — not supported, returns ENOTSUP
2. Symbolic links (symlink/readlink) — not supported, returns ENOTSUP
3. Extended attributes (setxattr/getxattr) — not supported, returns ENOTSUP
4. Special files (fifo, socket, block/char devices) — not supported
5. File locking (flock/lockf) — not implemented
6. Precise atime semantics — atime not tracked
7. Permission enforcement — chmod/chown accepted as no-op
8. truncate (non-zero size) — returns ENOTSUP
9. chown to non-root — ownership not persisted
10. rename RENAME_EXCHANGE flag — returns ENOTSUP

Failed Test Categories
---------------------
SUMMARY

# Extract failed test names and group them
if [ "${FAILED}" -gt 0 ]; then
  grep 'not ok' "${REPORT_FILE}" | head -50 >> "${SUMMARY_FILE}"
fi

echo ""
echo "=== Summary ==="
echo "Total: ${TOTAL}  Passed: ${PASSED}  Failed: ${FAILED}"
echo "Pass rate: $(awk "BEGIN {if (${TOTAL}>0) printf \"%.1f%%\", ${PASSED}*100/${TOTAL}; else print \"N/A\"}")"
echo ""
echo "Report saved to: ${REPORT_FILE}"
echo "Summary saved to: ${SUMMARY_FILE}"

# Cleanup test directory
rm -rf "${TEST_DIR}" 2>/dev/null || true

exit ${PROVE_EXIT}
