#!/usr/bin/env bash
# Cursor Agent stop hook — 任务结束时通知用户
# 当 Agent 的任务完成、中止或出错时触发
# 输入：通过 stdin 接收 JSON，包含 status, conversation_id, generation_id 等字段

set -euo pipefail

# 读取 stdin 中的 JSON payload
INPUT=$(cat)

# 解析字段（兼容无 jq 的环境）
STATUS=$(echo "$INPUT" | grep -o '"status":"[^"]*"' | head -1 | cut -d'"' -f4)
CONV_ID=$(echo "$INPUT" | grep -o '"conversation_id":"[^"]*"' | head -1 | cut -d'"' -f4)
GEN_ID=$(echo "$INPUT" | grep -o '"generation_id":"[^"]*"' | head -1 | cut -d'"' -f4)
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

# 日志目录
LOG_DIR="${HOME}/.cursor/agent-logs"
mkdir -p "$LOG_DIR"
LOG_FILE="${LOG_DIR}/task-history.log"

# 记录到日志
echo "[${TIMESTAMP}] status=${STATUS} conversation=${CONV_ID} generation=${GEN_ID}" >> "$LOG_FILE"

# 触发自动 reviewer（异步），仅在 completed 时真正执行。
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TRIGGER_SCRIPT="${PROJECT_ROOT}/.cursor/scripts/trigger-review-on-stop.sh"
if [[ -x "$TRIGGER_SCRIPT" ]]; then
  "$TRIGGER_SCRIPT" \
    --status "${STATUS:-unknown}" \
    --conversation "${CONV_ID:-unknown}" \
    --generation "${GEN_ID:-unknown}" >/dev/null 2>&1 &
fi

# macOS 通知（根据状态不同显示不同内容）
if [[ "$(uname)" == "Darwin" ]]; then
  case "$STATUS" in
    completed)
      osascript -e "display notification \"任务已完成 ✅\" with title \"Cursor Agent\" subtitle \"${CONV_ID:0:8}\" sound name \"Glass\"" 2>/dev/null || true
      ;;
    aborted)
      osascript -e "display notification \"任务被中止 ⚠️\" with title \"Cursor Agent\" subtitle \"请检查是否有未完成项\" sound name \"Basso\"" 2>/dev/null || true
      ;;
    error)
      osascript -e "display notification \"任务出错 ❌\" with title \"Cursor Agent\" subtitle \"请查看错误日志\" sound name \"Sosumi\"" 2>/dev/null || true
      ;;
  esac
fi
