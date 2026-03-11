#!/usr/bin/env bash
# Trigger reviewer checks after Cursor task completion.
#
# Usage:
#   bash .cursor/scripts/trigger-review-on-stop.sh --status completed --conversation <id> --generation <id>
# Optional:
#   --dry-run

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CURSOR_AGENT="${HOME}/.local/bin/cursor-agent"
LOG_DIR="${HOME}/.cursor/agent-logs"
STATE_DIR="${LOG_DIR}/review-trigger-state"
OUTPUT_DIR="${PROJECT_ROOT}/design/design-review-sessions"
GATE_DIR="${PROJECT_ROOT}/.cursor/review-gate"

STATUS=""
CONV_ID=""
GEN_ID=""
DRY_RUN=0
REVIEW_BLOCK_MODE="${REVIEW_BLOCK_MODE:-soft}" # off | soft | hard
RALPH_WIGGUM_LOOP="${RALPH_WIGGUM_LOOP:-1}"   # 1=评审不通过时自动修复再评，0=关闭
MAX_FIX_ROUNDS="${MAX_FIX_ROUNDS:-2}"        # 最多修复轮数

while [[ $# -gt 0 ]]; do
  case "$1" in
    --status)
      STATUS="${2:-}"
      shift 2
      ;;
    --conversation)
      CONV_ID="${2:-}"
      shift 2
      ;;
    --generation)
      GEN_ID="${2:-}"
      shift 2
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    *)
      echo "unknown arg: $1" >&2
      exit 2
      ;;
  esac
done

mkdir -p "$LOG_DIR" "$STATE_DIR" "$OUTPUT_DIR"
mkdir -p "$GATE_DIR"
TRIGGER_LOG="${LOG_DIR}/review-trigger.log"
FAIL_MARKER="${GATE_DIR}/FAIL"
PASS_MARKER="${GATE_DIR}/PASS"

ts() {
  date '+%Y-%m-%d %H:%M:%S'
}

log() {
  echo "[$(ts)] $*" >> "$TRIGGER_LOG"
}

if [[ "$STATUS" != "completed" ]]; then
  log "skip: status=$STATUS (only completed triggers reviewer)"
  exit 0
fi

if [[ -z "$GEN_ID" ]]; then
  GEN_ID="unknown-generation-$(date +%s)"
fi

STATE_FILE="${STATE_DIR}/${GEN_ID}.done"
if [[ -f "$STATE_FILE" ]]; then
  log "skip: generation already reviewed: $GEN_ID"
  exit 0
fi

if [[ ! -x "$CURSOR_AGENT" ]]; then
  log "skip: cursor-agent not found at $CURSOR_AGENT"
  exit 0
fi

if [[ ! -d "${PROJECT_ROOT}/.git" ]]; then
  log "skip: not a git repo: $PROJECT_ROOT"
  exit 0
fi

CHANGED_SUMMARY="$(git -C "$PROJECT_ROOT" status --porcelain)"
if [[ -z "$CHANGED_SUMMARY" ]]; then
  log "skip: no changed files"
  touch "$STATE_FILE"
  exit 0
fi

DIFF_CONTENT="$(git -C "$PROJECT_ROOT" diff -- . ':(exclude)design/design-review-sessions/*' | sed -n '1,900p')"
if [[ -z "$DIFF_CONTENT" ]]; then
  DIFF_CONTENT="[diff too large or empty after filters]"
fi

if [[ $DRY_RUN -eq 1 ]]; then
  log "dry-run: would run reviewers for generation=$GEN_ID conversation=$CONV_ID mode=$REVIEW_BLOCK_MODE"
  touch "$STATE_FILE"
  exit 0
fi

reviewer_prompt() {
  local reviewer_text="$1"
  cat <<EOF
[System Context]
${reviewer_text}

[User Prompt]
请对以下「本次任务变更」执行审查，重点关注可落地问题，并给出明确结论。

Project: ${PROJECT_ROOT}
Conversation: ${CONV_ID}
Generation: ${GEN_ID}

## Git Status (porcelain)
${CHANGED_SUMMARY}

## Git Diff (truncated)
${DIFF_CONTENT}

请严格按 reviewer 的 Output Format 输出。末尾增加一行：
ReviewerResult: PASS | NEEDS_REVISION | BLOCKER
EOF
}

run_one_reviewer() {
  local reviewer_name="$1"
  local reviewer_file="$2"
  local model="$3"
  local output_file="$4"

  if [[ ! -f "$reviewer_file" ]]; then
    log "skip reviewer=$reviewer_name missing file=$reviewer_file"
    return 0
  fi

  local reviewer_text
  reviewer_text="$(cat "$reviewer_file")"
  local prompt
  prompt="$(reviewer_prompt "$reviewer_text")"

  if "$CURSOR_AGENT" --model "$model" --output-format text --trust --force -p "$prompt" > "$output_file" 2>"${output_file}.err"; then
    log "reviewer=$reviewer_name done output=$output_file"
    return 0
  fi

  log "reviewer=$reviewer_name failed output=${output_file}.err"
  return 1
}

extract_reviewer_result() {
  local output_file="$1"
  if [[ ! -f "$output_file" ]]; then
    echo "BLOCKER"
    return 0
  fi

  local raw=""
  raw="$(awk -F':' '/ReviewerResult/ {print $2}' "$output_file" | tail -1 | tr -d '[:space:]' | tr '[:lower:]' '[:upper:]')"
  case "$raw" in
    PASS|NEEDS_REVISION|BLOCKER)
      echo "$raw"
      ;;
    *)
      # Missing/invalid result is treated as NEEDS_REVISION.
      echo "NEEDS_REVISION"
      ;;
  esac
}

notify_blocker() {
  local message="$1"
  if [[ "$(uname)" == "Darwin" ]]; then
    osascript -e "display notification \"${message}\" with title \"Cursor Reviewer Trigger\" subtitle \"自动评审结论\" sound name \"Sosumi\"" 2>/dev/null || true
  fi
}

run_fix_agent() {
  local summary_file="$1"
  local base="${summary_file%-summary.md}"
  local feedback_condensed
  feedback_condensed="$( { head -80 "$summary_file"
    for name in feasibility correctness maintainability design; do
      [[ -f "${base}-${name}.txt" ]] && echo "--- ${name} ---" && head -50 "${base}-${name}.txt"
    done; } 2>/dev/null | head -200)"
  local prompt="[Task] 根据以下评审反馈修复代码。修改后输出 DONE。

${feedback_condensed}

Project: ${PROJECT_ROOT}
Workspace: 使用 Read/Write 工具修改 ${PROJECT_ROOT} 下的文件。"
  log "fix_agent: starting for summary=$summary_file"
  if "$CURSOR_AGENT" --model "gpt-5.3-codex" --output-format text --trust --force \
      --workspace "$PROJECT_ROOT" -p "$prompt" > "${summary_file}.fix-out" 2>"${summary_file}.fix-err"; then
    log "fix_agent: done"
    return 0
  fi
  log "fix_agent: failed"
  return 1
}

AGENTS_DIR_PROJECT="${PROJECT_ROOT}/.cursor/agents"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
OVERALL_RESULT="PASS"
SUMMARY_FILE=""
fix_round=0

while true; do
  CHANGED_SUMMARY="$(git -C "$PROJECT_ROOT" status --porcelain)"
  DIFF_CONTENT="$(git -C "$PROJECT_ROOT" diff -- . ':(exclude)design/design-review-sessions/*' | sed -n '1,900p')"
  [[ -z "$DIFF_CONTENT" ]] && DIFF_CONTENT="[diff too large or empty after filters]"

  PREFIX="${OUTPUT_DIR}/auto-review-${TIMESTAMP}-${GEN_ID}-r${fix_round}"
  FAIL=0
  run_one_reviewer "feasibility" "${AGENTS_DIR_PROJECT}/reviewer-feasibility.md" "gpt-5.3-codex" "${PREFIX}-feasibility.txt" || FAIL=$((FAIL + 1))
  run_one_reviewer "correctness" "${AGENTS_DIR_PROJECT}/reviewer-correctness.md" "gpt-5.3-codex" "${PREFIX}-correctness.txt" || FAIL=$((FAIL + 1))
  run_one_reviewer "maintainability" "${AGENTS_DIR_PROJECT}/reviewer-maintainability.md" "gpt-5.3-codex" "${PREFIX}-maintainability.txt" || FAIL=$((FAIL + 1))
  run_one_reviewer "design" "${AGENTS_DIR_PROJECT}/reviewer-design.md" "gpt-5.3-codex" "${PREFIX}-design.txt" || FAIL=$((FAIL + 1))

  R_FEASIBILITY="$(extract_reviewer_result "${PREFIX}-feasibility.txt")"
  R_CORRECTNESS="$(extract_reviewer_result "${PREFIX}-correctness.txt")"
  R_MAINTAINABILITY="$(extract_reviewer_result "${PREFIX}-maintainability.txt")"
  R_DESIGN="$(extract_reviewer_result "${PREFIX}-design.txt")"

  OVERALL_RESULT="PASS"
  for r in "$R_FEASIBILITY" "$R_CORRECTNESS" "$R_MAINTAINABILITY" "$R_DESIGN"; do
    if [[ "$r" == "BLOCKER" ]]; then OVERALL_RESULT="BLOCKER"; break; fi
    if [[ "$r" == "NEEDS_REVISION" && "$OVERALL_RESULT" == "PASS" ]]; then OVERALL_RESULT="NEEDS_REVISION"; fi
  done

  SUMMARY_FILE="${PREFIX}-summary.md"
{
  echo "# Auto Review Summary"
  echo
  echo "- conversation: ${CONV_ID}"
  echo "- generation: ${GEN_ID}"
  echo "- status: ${STATUS}"
  echo "- project: ${PROJECT_ROOT}"
  echo "- timestamp: $(ts)"
  echo "- review_block_mode: ${REVIEW_BLOCK_MODE}"
  echo
  echo "## Outputs"
  echo "- ${PREFIX}-feasibility.txt"
  echo "- ${PREFIX}-correctness.txt"
  echo "- ${PREFIX}-maintainability.txt"
  echo "- ${PREFIX}-design.txt"
  echo
  echo "## Reviewer Results"
  echo "- feasibility: ${R_FEASIBILITY}"
  echo "- correctness: ${R_CORRECTNESS}"
  echo "- maintainability: ${R_MAINTAINABILITY}"
  echo "- design: ${R_DESIGN}"
  echo "- overall: ${OVERALL_RESULT}"
  echo "- round: ${fix_round}"
  echo
  if [[ $FAIL -eq 0 ]]; then
    echo "## Result"
    echo "- trigger execution: PASS"
  else
    echo "## Result"
    echo "- trigger execution: PARTIAL_FAIL (${FAIL} reviewer failed)"
  fi
} > "$SUMMARY_FILE"

  if [[ "$OVERALL_RESULT" == "PASS" ]]; then
    break
  fi
  if [[ "$RALPH_WIGGUM_LOOP" != "1" ]] || [[ "$fix_round" -ge "$MAX_FIX_ROUNDS" ]]; then
    log "ralph_wiggum: stop (loop=$RALPH_WIGGUM_LOOP round=$fix_round max=$MAX_FIX_ROUNDS)"
    break
  fi
  if ! run_fix_agent "$SUMMARY_FILE"; then
    log "ralph_wiggum: fix agent failed, stop"
    break
  fi
  fix_round=$((fix_round + 1))
  log "ralph_wiggum: starting round $fix_round"
done

rm -f "$PASS_MARKER" "$FAIL_MARKER"
case "$REVIEW_BLOCK_MODE" in
  off)
    log "gate=off overall=$OVERALL_RESULT summary=$SUMMARY_FILE"
    ;;
  soft)
    if [[ "$OVERALL_RESULT" == "BLOCKER" ]]; then
      echo "BLOCKER ${TIMESTAMP} ${GEN_ID} ${SUMMARY_FILE}" > "$FAIL_MARKER"
      notify_blocker "检测到 BLOCKER，请查看自动评审报告"
      log "gate=soft BLOCKER fail_marker=$FAIL_MARKER summary=$SUMMARY_FILE"
    else
      echo "${OVERALL_RESULT} ${TIMESTAMP} ${GEN_ID} ${SUMMARY_FILE}" > "$PASS_MARKER"
      log "gate=soft overall=$OVERALL_RESULT pass_marker=$PASS_MARKER summary=$SUMMARY_FILE"
    fi
    ;;
  hard)
    if [[ "$OVERALL_RESULT" == "PASS" ]]; then
      echo "PASS ${TIMESTAMP} ${GEN_ID} ${SUMMARY_FILE}" > "$PASS_MARKER"
      log "gate=hard pass pass_marker=$PASS_MARKER summary=$SUMMARY_FILE"
    else
      echo "${OVERALL_RESULT} ${TIMESTAMP} ${GEN_ID} ${SUMMARY_FILE}" > "$FAIL_MARKER"
      notify_blocker "自动评审未通过(${OVERALL_RESULT})，已触发硬阻断"
      log "gate=hard blocked overall=$OVERALL_RESULT fail_marker=$FAIL_MARKER summary=$SUMMARY_FILE"
    fi
    ;;
  *)
    echo "invalid REVIEW_BLOCK_MODE=${REVIEW_BLOCK_MODE}" > "$FAIL_MARKER"
    log "invalid mode REVIEW_BLOCK_MODE=$REVIEW_BLOCK_MODE; wrote fail marker"
    ;;
esac

touch "$STATE_FILE"
log "done: generation=$GEN_ID fail_count=$FAIL overall=$OVERALL_RESULT summary=$SUMMARY_FILE"

exit 0
