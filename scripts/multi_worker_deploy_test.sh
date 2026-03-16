#!/usr/bin/env bash
# Multi-process deployment test for FluxCache.
# Starts 1 master + 2 workers as separate processes, then exercises the CLI
# to mount, write, read, stat, and verify worker-down resilience.
#
# Usage: ./scripts/multi_worker_deploy_test.sh [build_dir]
#   build_dir: path to cmake build tree (default: auto-detect)

set -euo pipefail

# ── colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

pass()  { echo -e "${GREEN}[PASS]${RESET} $*"; }
fail()  { echo -e "${RED}[FAIL]${RESET} $*"; }
info()  { echo -e "${CYAN}[INFO]${RESET} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${RESET} $*"; }
step()  { echo -e "${BOLD}==> $*${RESET}"; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── locate binaries ─────────────────────────────────────────────────────────
find_binary() {
  local name="$1"
  local candidates=(
    "${BUILD_DIR}/src/master/${name}"
    "${BUILD_DIR}/src/worker/${name}"
    "${BUILD_DIR}/src/client/${name}"
    "${BUILD_DIR}/src/client/cli/${name}"
    "${BUILD_DIR}/${name}"
    "${PROJECT_ROOT}/${name}"
  )
  for c in "${candidates[@]}"; do
    if [[ -x "$c" ]]; then
      echo "$c"
      return
    fi
  done
  echo ""
}

BUILD_DIR="${1:-}"
if [[ -z "$BUILD_DIR" ]]; then
  for candidate in "$PROJECT_ROOT/build" "$PROJECT_ROOT/cmake-build-debug" "$PROJECT_ROOT/cmake-build-release"; do
    if [[ -d "$candidate" ]]; then
      BUILD_DIR="$candidate"
      break
    fi
  done
fi
if [[ -z "$BUILD_DIR" || ! -d "$BUILD_DIR" ]]; then
  fail "Cannot find build directory. Pass it as first argument or run build.sh first."
  exit 1
fi
info "Build directory: $BUILD_DIR"

MASTER_BIN=$(find_binary fluxcache-master)
WORKER_BIN=$(find_binary fluxcache-worker)
CLI_BIN=$(find_binary fluxcache-cli)

for bin_var in MASTER_BIN WORKER_BIN CLI_BIN; do
  if [[ -z "${!bin_var}" ]]; then
    fail "Binary not found: $bin_var  (looked under $BUILD_DIR)"
    exit 1
  fi
done

info "Master binary : $MASTER_BIN"
info "Worker binary : $WORKER_BIN"
info "CLI binary    : $CLI_BIN"

# ── ports ────────────────────────────────────────────────────────────────────
MASTER_PORT=19100
WORKER1_PORT=19101
WORKER2_PORT=19102

# ── temp dirs ────────────────────────────────────────────────────────────────
TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/fluxcache_deploy_test.XXXXXX")
MASTER_DB="$TMPROOT/master_db"
WORKER1_DATA="$TMPROOT/worker1_data"
WORKER2_DATA="$TMPROOT/worker2_data"
UFS_ROOT="$TMPROOT/ufs_root"
mkdir -p "$MASTER_DB" "$WORKER1_DATA" "$WORKER2_DATA" "$UFS_ROOT"
info "Temp root: $TMPROOT"

# ── PIDs to clean up ────────────────────────────────────────────────────────
PIDS=()

cleanup() {
  info "Cleaning up..."
  for pid in "${PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  done
  rm -rf "$TMPROOT"
  info "Done."
}
trap cleanup EXIT

# ── generate YAML configs ───────────────────────────────────────────────────
gen_config() {
  local dest="$1"
  local master_host="${2:-127.0.0.1}"
  local master_port="${3:-$MASTER_PORT}"
  local master_db="${4:-$MASTER_DB}"
  local worker_host="${5:-127.0.0.1}"
  local worker_port="${6:-0}"
  local worker_data="${7:-$WORKER1_DATA}"
  local ufs_path="${8:-$UFS_ROOT}"

  cat > "$dest" <<YAML
fluxcache:
  master:
    host: "${master_host}"
    port: ${master_port}
    db_path: "${master_db}"
  worker:
    host: "${worker_host}"
    port: ${worker_port}
    data_dir: "${worker_data}"
    heartbeat_interval_ms: 1000
    eviction_policy: "lru"
  client:
    master_host: "${master_host}"
    master_port: ${master_port}
    channel_pool_size: 2
    retry_max_attempts: 3
    retry_initial_delay_ms: 50
    circuit_breaker_enabled: false
  ufs:
    type: "localfs"
    path: "${ufs_path}"
YAML
}

MASTER_CONF="$TMPROOT/master.yaml"
WORKER1_CONF="$TMPROOT/worker1.yaml"
WORKER2_CONF="$TMPROOT/worker2.yaml"
CLIENT_CONF="$TMPROOT/client.yaml"

gen_config "$MASTER_CONF"  "127.0.0.1" "$MASTER_PORT" "$MASTER_DB" "127.0.0.1" 0         "$WORKER1_DATA" "$UFS_ROOT"
gen_config "$WORKER1_CONF" "127.0.0.1" "$MASTER_PORT" "$MASTER_DB" "127.0.0.1" "$WORKER1_PORT" "$WORKER1_DATA" "$UFS_ROOT"
gen_config "$WORKER2_CONF" "127.0.0.1" "$MASTER_PORT" "$MASTER_DB" "127.0.0.1" "$WORKER2_PORT" "$WORKER2_DATA" "$UFS_ROOT"
gen_config "$CLIENT_CONF"  "127.0.0.1" "$MASTER_PORT" "$MASTER_DB" "127.0.0.1" 0         "$WORKER1_DATA" "$UFS_ROOT"

CLI="$CLI_BIN --config $CLIENT_CONF"

# ── helper: wait for gRPC port ──────────────────────────────────────────────
wait_for_port() {
  local port="$1"
  local label="$2"
  local timeout="${3:-10}"
  local elapsed=0
  while ! (echo >/dev/tcp/127.0.0.1/"$port") 2>/dev/null; do
    sleep 0.3
    elapsed=$(echo "$elapsed + 0.3" | bc)
    if (( $(echo "$elapsed >= $timeout" | bc -l) )); then
      fail "$label did not become ready on port $port within ${timeout}s"
      exit 1
    fi
  done
}

# ── test counters ────────────────────────────────────────────────────────────
TESTS_TOTAL=0
TESTS_PASSED=0
TESTS_FAILED=0

assert_ok() {
  local label="$1"; shift
  TESTS_TOTAL=$((TESTS_TOTAL + 1))
  if "$@" ; then
    pass "$label"
    TESTS_PASSED=$((TESTS_PASSED + 1))
  else
    fail "$label (exit=$?)"
    TESTS_FAILED=$((TESTS_FAILED + 1))
  fi
}

assert_eq() {
  local label="$1"
  local expected="$2"
  local actual="$3"
  TESTS_TOTAL=$((TESTS_TOTAL + 1))
  if [[ "$actual" == "$expected" ]]; then
    pass "$label"
    TESTS_PASSED=$((TESTS_PASSED + 1))
  else
    fail "$label — expected='$expected' got='$actual'"
    TESTS_FAILED=$((TESTS_FAILED + 1))
  fi
}

assert_contains() {
  local label="$1"
  local haystack="$2"
  local needle="$3"
  TESTS_TOTAL=$((TESTS_TOTAL + 1))
  if [[ "$haystack" == *"$needle"* ]]; then
    pass "$label"
    TESTS_PASSED=$((TESTS_PASSED + 1))
  else
    fail "$label — output does not contain '$needle'"
    TESTS_FAILED=$((TESTS_FAILED + 1))
  fi
}

# ═══════════════════════════════════════════════════════════════════════════════
# Phase 1: Start master
# ═══════════════════════════════════════════════════════════════════════════════
step "Phase 1: Starting master (port $MASTER_PORT)"
"$MASTER_BIN" --config "$MASTER_CONF" &
MASTER_PID=$!
PIDS+=("$MASTER_PID")
wait_for_port "$MASTER_PORT" "Master"
info "Master ready (pid=$MASTER_PID)"

# ═══════════════════════════════════════════════════════════════════════════════
# Phase 2: Start workers
# ═══════════════════════════════════════════════════════════════════════════════
step "Phase 2: Starting worker1 (port $WORKER1_PORT) and worker2 (port $WORKER2_PORT)"
"$WORKER_BIN" --config "$WORKER1_CONF" &
WORKER1_PID=$!
PIDS+=("$WORKER1_PID")

"$WORKER_BIN" --config "$WORKER2_CONF" &
WORKER2_PID=$!
PIDS+=("$WORKER2_PID")

wait_for_port "$WORKER1_PORT" "Worker1"
wait_for_port "$WORKER2_PORT" "Worker2"
info "Worker1 ready (pid=$WORKER1_PID), Worker2 ready (pid=$WORKER2_PID)"

# Workers auto-register via heartbeat; give them time.
info "Waiting for heartbeat registration..."
sleep 3

# ═══════════════════════════════════════════════════════════════════════════════
# Phase 3: Mount UFS
# ═══════════════════════════════════════════════════════════════════════════════
step "Phase 3: Mount local UFS at /mnt"
assert_ok "Mount local UFS" $CLI mount /mnt "local://${UFS_ROOT}"

MOUNT_OUT=$($CLI ls-mounts 2>&1) || true
assert_contains "ls-mounts shows /mnt" "$MOUNT_OUT" "/mnt"

# ═══════════════════════════════════════════════════════════════════════════════
# Phase 4: Write + Read a single file
# ═══════════════════════════════════════════════════════════════════════════════
step "Phase 4: Write and read a single file"
assert_ok "Write /mnt/hello.txt" $CLI write /mnt/hello.txt "HelloFluxCache"

READ_OUT=$($CLI read /mnt/hello.txt 2>&1) || true
assert_eq "Read /mnt/hello.txt content" "HelloFluxCache" "$READ_OUT"

# ═══════════════════════════════════════════════════════════════════════════════
# Phase 5: Stat
# ═══════════════════════════════════════════════════════════════════════════════
step "Phase 5: Stat file"
STAT_OUT=$($CLI stat /mnt/hello.txt 2>&1) || true
assert_contains "Stat shows size" "$STAT_OUT" "size: 14"

# ═══════════════════════════════════════════════════════════════════════════════
# Phase 6: Multiple files
# ═══════════════════════════════════════════════════════════════════════════════
step "Phase 6: Write and read multiple files"
NUM_FILES=5
ALL_FILES_OK=true
for i in $(seq 1 $NUM_FILES); do
  DATA="payload_${i}_$(printf '%0100d' $i)"
  if ! $CLI write "/mnt/file_${i}.dat" "$DATA" >/dev/null 2>&1; then
    fail "Write /mnt/file_${i}.dat"
    ALL_FILES_OK=false
  fi
done

for i in $(seq 1 $NUM_FILES); do
  DATA="payload_${i}_$(printf '%0100d' $i)"
  GOT=$($CLI read "/mnt/file_${i}.dat" 2>&1) || true
  if [[ "$GOT" != "$DATA" ]]; then
    fail "Read-back /mnt/file_${i}.dat mismatch"
    ALL_FILES_OK=false
  fi
done

TESTS_TOTAL=$((TESTS_TOTAL + 1))
if $ALL_FILES_OK; then
  pass "Write+Read $NUM_FILES files"
  TESTS_PASSED=$((TESTS_PASSED + 1))
else
  TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# ═══════════════════════════════════════════════════════════════════════════════
# Phase 7: Worker-down resilience
# ═══════════════════════════════════════════════════════════════════════════════
step "Phase 7: Worker-down resilience — killing worker2 (pid=$WORKER2_PID)"
kill "$WORKER2_PID" 2>/dev/null || true
wait "$WORKER2_PID" 2>/dev/null || true
info "Worker2 stopped"

sleep 1

RESILIENCE_OK=0
RESILIENCE_FAIL=0
for i in $(seq 1 $NUM_FILES); do
  DATA="payload_${i}_$(printf '%0100d' $i)"
  GOT=$($CLI read "/mnt/file_${i}.dat" 2>&1) || true
  if [[ "$GOT" == "$DATA" ]]; then
    RESILIENCE_OK=$((RESILIENCE_OK + 1))
  else
    RESILIENCE_FAIL=$((RESILIENCE_FAIL + 1))
  fi
done

TESTS_TOTAL=$((TESTS_TOTAL + 1))
if [[ $RESILIENCE_OK -gt 0 ]]; then
  pass "Worker-down: $RESILIENCE_OK/$NUM_FILES files still readable after worker2 killed"
  TESTS_PASSED=$((TESTS_PASSED + 1))
else
  fail "Worker-down: 0/$NUM_FILES files readable (expected at least some via live worker or UFS fallback)"
  TESTS_FAILED=$((TESTS_FAILED + 1))
fi
if [[ $RESILIENCE_FAIL -gt 0 ]]; then
  warn "$RESILIENCE_FAIL/$NUM_FILES files unreadable (expected if routed to dead worker without UFS fallback)"
fi

# Re-read the hello.txt that was written before any worker death
READ_HELLO=$($CLI read /mnt/hello.txt 2>&1) || true
assert_eq "Read /mnt/hello.txt after worker2 down" "HelloFluxCache" "$READ_HELLO"

# ═══════════════════════════════════════════════════════════════════════════════
# Summary
# ═══════════════════════════════════════════════════════════════════════════════
echo ""
echo -e "${BOLD}════════════════════════════════════════${RESET}"
echo -e "${BOLD} Test Summary${RESET}"
echo -e "${BOLD}════════════════════════════════════════${RESET}"
echo -e "  Total  : $TESTS_TOTAL"
echo -e "  ${GREEN}Passed : $TESTS_PASSED${RESET}"
if [[ $TESTS_FAILED -gt 0 ]]; then
  echo -e "  ${RED}Failed : $TESTS_FAILED${RESET}"
  echo ""
  fail "Some tests failed."
  exit 1
else
  echo -e "  Failed : 0"
  echo ""
  pass "All tests passed!"
  exit 0
fi
