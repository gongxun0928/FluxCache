#!/usr/bin/env bash
# Multi-Agent Plan:
# - fast 模式（默认）: Planner (GPT-5.3 Codex) + Reviewer (GPT-5.3 Codex)
# - strict 模式:      Planner (GPT-5.3 Codex) + Reviewer (Opus 4.6 Thinking)
#
# Phase 1: Plan 设计 — Planner 出方案 → Reviewer 评审 → 迭代至共识
# Phase 2: TODO 对齐 — Planner 拆 TODO → Reviewer 评审对齐 → 迭代至共识
#
# 用法: bash <script_path> <topic> <requirements_file> [--mode fast|strict]
#   - 项目安装: bash .cursor/skills/multi-agent-design-review/scripts/multi-agent-plan.sh ...
#   - 全局安装: bash ~/.cursor/skills/multi-agent-design-review/scripts/multi-agent-plan.sh ...
#   topic:            方案主题（用于文件命名，如 "inode-tree-concurrency"）
#   requirements_file: 需求文件路径（相对项目根目录）
#   --mode:
#     - fast   (默认): 更快，适合中低风险任务
#     - strict: 更稳，适合高风险/跨模块/接口不兼容任务

set -euo pipefail

MODE="fast"
PLANNER_MODEL="gpt-5.3-codex"
REVIEWER_MODEL="gpt-5.3-codex"
MAX_PLAN_ROUNDS=2
MAX_TODO_ROUNDS=1

CURSOR_AGENT="${HOME}/.local/bin/cursor-agent"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 双模式 PROJECT_ROOT 检测：
# - 项目安装 (.cursor/skills/.../scripts/)：从 scripts 上溯 4 层到项目根
# - 全局安装 (~/.cursor/skills/.../scripts/)：使用当前工作目录（Agent 在项目根执行）
CANDIDATE_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
if [[ "$CANDIDATE_ROOT" == "$HOME" ]] || [[ "$CANDIDATE_ROOT" == "$HOME/.cursor" ]]; then
  PROJECT_ROOT="${WORKSPACE:-$PWD}"
else
  PROJECT_ROOT="$CANDIDATE_ROOT"
fi

usage() {
  cat <<'EOF'
用法: bash multi-agent-plan.sh <topic> <requirements_file> [--mode fast|strict]

参数:
  <topic>             方案主题（用于文件命名）
  <requirements_file> 需求文件路径（相对项目根目录）
  --mode              fast（默认）或 strict
EOF
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

TOPIC="${1:?用法: $0 <topic> <requirements_file> [--mode fast|strict]}"
REQ_INPUT="${2:?用法: $0 <topic> <requirements_file> [--mode fast|strict]}"
shift 2

while [ "$#" -gt 0 ]; do
  case "$1" in
    --mode)
      MODE="${2:-}"
      shift 2
      ;;
    *)
      echo "错误: 未知参数 $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

case "$MODE" in
  fast)
    PLANNER_MODEL="gpt-5.3-codex"
    REVIEWER_MODEL="gpt-5.3-codex"
    MAX_PLAN_ROUNDS=2
    MAX_TODO_ROUNDS=1
    ;;
  strict)
    PLANNER_MODEL="gpt-5.3-codex"
    REVIEWER_MODEL="opus-4.6-thinking"
    MAX_PLAN_ROUNDS=3
    MAX_TODO_ROUNDS=2
    ;;
  *)
    echo "错误: --mode 仅支持 fast 或 strict，当前为: $MODE" >&2
    usage >&2
    exit 1
    ;;
esac

if [ -f "$PROJECT_ROOT/$REQ_INPUT" ]; then
  REQUIREMENTS=$(cat "$PROJECT_ROOT/$REQ_INPUT")
elif [ -f "$REQ_INPUT" ]; then
  REQUIREMENTS=$(cat "$REQ_INPUT")
else
  REQUIREMENTS="$REQ_INPUT"
fi

TIMESTAMP=$(date +%Y%m%d-%H%M)
PLAN_FILE="${PROJECT_ROOT}/plan/plan_${TOPIC}.md"
TODO_FILE="${PROJECT_ROOT}/plan/todo-${TOPIC}.md"
SESSION_DIR="${PROJECT_ROOT}/design/design-review-sessions"
DISCUSSION="${SESSION_DIR}/${TOPIC}-discussion-${TIMESTAMP}.md"
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

mkdir -p "$SESSION_DIR" "$(dirname "$PLAN_FILE")"

log() { echo "[$(date +%H:%M:%S)] $*" >&2; }

call_agent() {
  local model=$1 prompt_file=$2 output_file=$3 label=$4
  log "[$label] 启动 (model: $model)..."
  local start_s=$SECONDS
  if "$CURSOR_AGENT" --model "$model" -p --trust --force \
      --workspace "$PROJECT_ROOT" \
      < "$prompt_file" > "$output_file" 2>"${output_file}.err"; then
    local lines; lines=$(wc -l < "$output_file" | tr -d ' ')
    log "[$label] 完成 (${lines} 行, $((SECONDS - start_s))s)"
  else
    log "[$label] 失败 (exit=$?, $((SECONDS - start_s))s)"
    [ -s "${output_file}.err" ] && head -5 "${output_file}.err" >&2
    return 1
  fi
}

verdict_is_pass() {
  grep -qiE '评审结论[：:].*(通过|approved|no major)' "$1" 2>/dev/null
}

split_output() {
  local full=$1 separator=$2 response_out=$3 content_out=$4
  if grep -qF -- "$separator" "$full"; then
    sed -n "1,/${separator}/p" "$full" | sed '$ d' > "$response_out"
    sed -n "/${separator}/,\$ p" "$full" | sed '1 d' > "$content_out"
    return 0
  else
    cp "$full" "$response_out"
    : > "$content_out"
    return 1
  fi
}

append_log() {
  local title=$1 file=$2
  { echo ""; echo "## $title"; echo ""; cat "$file"; echo ""; echo "---"; } >> "$DISCUSSION"
}

# ============================================================
# Discussion log header
# ============================================================
cat > "$DISCUSSION" <<HDR
# Multi-Agent Plan Discussion: ${TOPIC}

> 时间: $(date '+%Y-%m-%d %H:%M')
> Planner: ${PLANNER_MODEL}
> Reviewer: ${REVIEWER_MODEL}
> Plan rounds 上限: ${MAX_PLAN_ROUNDS}, TODO rounds 上限: ${MAX_TODO_ROUNDS}

---
HDR

# ============================================================
# Phase 1: Plan Design
# ============================================================
phase1() {
  log "========== Phase 1: Plan Design =========="

  # --- Round 0: Planner 初稿 ---
  cat > "$TMPDIR/p-init.prompt" <<'SYSEOF'
你是技术方案设计师（Planner）。

## 角色
- 客观中立，基于正确性和工程最佳实践设计方案
- 你可以使用 Read 工具阅读项目源码理解现有架构
- 请先阅读 AGENTS.md 了解项目规范和 Plan 格式要求

SYSEOF
  cat >> "$TMPDIR/p-init.prompt" <<REQEOF
## 任务
基于以下需求设计完整的技术方案：

${REQUIREMENTS}

## 输出要求
直接输出完整技术方案（markdown），包含：
- Goal
- 核心设计（数据结构、算法、接口、详细操作流程）
- Implementation Steps（分步实施）
- Risks & Mitigations
- To Confirm

不要输出开场白或结尾寒暄，直接输出方案正文。
REQEOF

  call_agent "$PLANNER_MODEL" "$TMPDIR/p-init.prompt" "$TMPDIR/plan-r0.txt" "Planner-Init"
  cp "$TMPDIR/plan-r0.txt" "$PLAN_FILE"
  append_log "Phase 1 Round 0: Planner 初稿（摘要前 30 行）" <(head -30 "$PLAN_FILE")

  # --- Iterative Review Rounds ---
  local plan_round_count=0
  for round in $(seq 1 "$MAX_PLAN_ROUNDS"); do
    log "--- Plan Round $round/$MAX_PLAN_ROUNDS ---"
    plan_round_count=$round
    local PLAN_CONTENT; PLAN_CONTENT=$(cat "$PLAN_FILE")

    # Reviewer 评审
    local prev_resp=""
    [ -f "$TMPDIR/p-resp-r$((round-1)).txt" ] && prev_resp=$(cat "$TMPDIR/p-resp-r$((round-1)).txt")

    cat > "$TMPDIR/r-r${round}.prompt" <<REOF
你是技术方案评审员（Reviewer）。

## 角色
- 客观中立，不附和 Planner，从正确性出发公正评审
- 每个问题给出具体论据，不做空泛评价
- 你可以使用 Read 工具阅读项目源码验证方案细节
- 如果 Planner 上轮已充分回应了你的建议且理由合理，接受并不再纠缠

## 评审维度
1. 正确性：边界条件、并发安全、逻辑漏洞、错误处理
2. 可维护性：复杂度、架构一致性、可测试性
3. 可行性：实现难度、依赖、迁移风险
4. 性能：瓶颈、扩展性、资源占用

## 当前方案
${PLAN_CONTENT}

$([ -n "$prev_resp" ] && echo "## Planner 上轮回应
$prev_resp")

## 输出格式

## 评审结论：[通过/需修订]

（按四维度逐项评审，列出改进建议汇总）
REOF

    call_agent "$REVIEWER_MODEL" "$TMPDIR/r-r${round}.prompt" "$TMPDIR/review-r${round}.txt" "Reviewer-R${round}"
    append_log "Phase 1 Round ${round}: Reviewer 评审" "$TMPDIR/review-r${round}.txt"

    if verdict_is_pass "$TMPDIR/review-r${round}.txt"; then
      log "✅ Reviewer 通过！Plan 设计完成 (Round $round)"
      break
    fi

    if [ "$round" -eq "$MAX_PLAN_ROUNDS" ]; then
      log "⚠️  达到最大 Plan 轮数 ($MAX_PLAN_ROUNDS)"
      break
    fi

    # Planner 回应 + 更新
    local REVIEW; REVIEW=$(cat "$TMPDIR/review-r${round}.txt")
    cat > "$TMPDIR/p-r${round}.prompt" <<PEOF
你是技术方案设计师（Planner）。评审员对你的方案提出了反馈。

## 角色
- 客观评估每条反馈
- 合理的接受并更新方案，说明具体修改了什么
- 不合理的明确说明拒绝理由和技术论据
- 不要无条件附和，也不要固执己见

## 当前方案
${PLAN_CONTENT}

## 评审反馈
${REVIEW}

## 输出要求
⚠️ 不要使用 Write 工具写文件。将所有内容直接输出到终端。
按以下格式输出（严格遵守分隔符，分隔符必须单独一行）：

[对反馈的逐条回应：接受/拒绝 + 理由]

---UPDATED_PLAN---

[完整的更新后方案全文（不是 diff，是可直接保存的完整文档）]
PEOF

    local plan_mtime_before; plan_mtime_before=$(stat -f %m "$PLAN_FILE" 2>/dev/null || echo 0)

    call_agent "$PLANNER_MODEL" "$TMPDIR/p-r${round}.prompt" "$TMPDIR/p-full-r${round}.txt" "Planner-R${round}"

    if split_output "$TMPDIR/p-full-r${round}.txt" "---UPDATED_PLAN---" \
        "$TMPDIR/p-resp-r${round}.txt" "$TMPDIR/p-plan-r${round}.txt"; then
      local updated_lines; updated_lines=$(wc -l < "$TMPDIR/p-plan-r${round}.txt" | tr -d ' ')
      if [ "$updated_lines" -gt 10 ]; then
        cp "$TMPDIR/p-plan-r${round}.txt" "$PLAN_FILE"
        log "Plan 已更新 ($updated_lines 行, 来自 stdout 分隔符拆分)"
      else
        log "⚠️  分隔符后内容过短 ($updated_lines 行)，检查文件..."
      fi
    else
      log "stdout 无分隔符，检查 Planner 是否直接写了文件..."
      cp "$TMPDIR/p-full-r${round}.txt" "$TMPDIR/p-resp-r${round}.txt"
    fi

    local plan_mtime_after; plan_mtime_after=$(stat -f %m "$PLAN_FILE" 2>/dev/null || echo 0)
    if [ "$plan_mtime_after" != "$plan_mtime_before" ] && [ ! -s "$TMPDIR/p-plan-r${round}.txt" ]; then
      local file_lines; file_lines=$(wc -l < "$PLAN_FILE" | tr -d ' ')
      log "Plan 已由 Planner 直接写入文件 ($file_lines 行)"
    fi

    append_log "Phase 1 Round ${round}: Planner 回应" "$TMPDIR/p-resp-r${round}.txt"
  done

  log "Plan 完成: $PLAN_FILE (经过 $plan_round_count 轮评审)"
}

# ============================================================
# Phase 2: TODO Creation & Alignment
# ============================================================
phase2() {
  log "========== Phase 2: TODO Alignment =========="

  local PLAN_CONTENT; PLAN_CONTENT=$(cat "$PLAN_FILE")

  # --- Round 0: Planner 创建 TODO ---
  cat > "$TMPDIR/pt-init.prompt" <<TEOF
你是技术方案设计师（Planner）。请基于已确定的技术方案创建实施 TODO 列表。

## 原则
- TODO 必须与 Plan 严格对齐：每个 Plan Step 对应一个或多个 TODO 任务
- 每个任务包含：目标、依赖、变更文件、实现要点、验收标准
- 明确任务间的依赖关系（哪些可并行）

## 技术方案
${PLAN_CONTENT}

## 输出要求
直接输出完整 TODO 文档（markdown），包含：
- 关联 Plan 文件路径
- 任务总览与依赖关系图
- 每个任务的详细描述

不要输出开场白，直接输出 TODO 正文。
TEOF

  call_agent "$PLANNER_MODEL" "$TMPDIR/pt-init.prompt" "$TMPDIR/todo-r0.txt" "Planner-TODO-Init"
  cp "$TMPDIR/todo-r0.txt" "$TODO_FILE"
  append_log "Phase 2 Round 0: Planner TODO 初稿（摘要前 30 行）" <(head -30 "$TODO_FILE")

  # --- Iterative Alignment Rounds ---
  local todo_round_count=0
  for round in $(seq 1 "$MAX_TODO_ROUNDS"); do
    log "--- TODO Round $round/$MAX_TODO_ROUNDS ---"
    todo_round_count=$round
    local TODO_CONTENT; TODO_CONTENT=$(cat "$TODO_FILE")

    local prev_resp=""
    [ -f "$TMPDIR/pt-resp-r$((round-1)).txt" ] && prev_resp=$(cat "$TMPDIR/pt-resp-r$((round-1)).txt")

    # Reviewer 评审对齐
    cat > "$TMPDIR/rt-r${round}.prompt" <<RTEOF
你是技术方案评审员（Reviewer）。请评审 TODO 与 Plan 的对齐情况。

## 角色
- 重点检查 TODO 是否完整覆盖 Plan 中的所有 Step 和关键设计点
- 检查实现要点是否准确反映 Plan 中的细节
- 检查依赖关系和验收标准是否合理
- 客观中立，不附和

## 技术方案（Plan）
${PLAN_CONTENT}

## TODO 列表
${TODO_CONTENT}

$([ -n "$prev_resp" ] && echo "## Planner 上轮回应
$prev_resp")

## 输出格式

## 评审结论：[通过/需修订]

## 匹配性检查
[Plan Step → TODO Task 的对应关系及一致性]

## 遗漏与问题
[TODO 中缺失或不准确的内容]

## 改进建议
[具体修改建议]
RTEOF

    call_agent "$REVIEWER_MODEL" "$TMPDIR/rt-r${round}.prompt" "$TMPDIR/rtrev-r${round}.txt" "Reviewer-TODO-R${round}"
    append_log "Phase 2 Round ${round}: Reviewer TODO 评审" "$TMPDIR/rtrev-r${round}.txt"

    if verdict_is_pass "$TMPDIR/rtrev-r${round}.txt"; then
      log "✅ Reviewer 通过！TODO 对齐完成 (Round $round)"
      break
    fi

    if [ "$round" -eq "$MAX_TODO_ROUNDS" ]; then
      log "⚠️  达到最大 TODO 轮数 ($MAX_TODO_ROUNDS)"
      break
    fi

    # Planner 更新 TODO
    local REVIEW; REVIEW=$(cat "$TMPDIR/rtrev-r${round}.txt")
    cat > "$TMPDIR/pt-r${round}.prompt" <<PTEOF
你是技术方案设计师（Planner）。评审员对 TODO 提出了反馈。

## 原则
- 客观评估每条反馈，确保 TODO 与 Plan 严格对齐
- 合理的接受，不合理的说明拒绝理由

## 技术方案（Plan）
${PLAN_CONTENT}

## 当前 TODO
${TODO_CONTENT}

## 评审反馈
${REVIEW}

## 输出要求
⚠️ 不要使用 Write 工具写文件。将所有内容直接输出到终端。
按以下格式输出（严格遵守分隔符，分隔符必须单独一行）：

[对反馈的逐条回应：接受/拒绝 + 理由]

---UPDATED_TODO---

[完整的更新后 TODO 全文（不是 diff，是可直接保存的完整文档）]
PTEOF

    local todo_mtime_before; todo_mtime_before=$(stat -f %m "$TODO_FILE" 2>/dev/null || echo 0)

    call_agent "$PLANNER_MODEL" "$TMPDIR/pt-r${round}.prompt" "$TMPDIR/pt-full-r${round}.txt" "Planner-TODO-R${round}"

    if split_output "$TMPDIR/pt-full-r${round}.txt" "---UPDATED_TODO---" \
        "$TMPDIR/pt-resp-r${round}.txt" "$TMPDIR/pt-todo-r${round}.txt"; then
      local updated_lines; updated_lines=$(wc -l < "$TMPDIR/pt-todo-r${round}.txt" | tr -d ' ')
      if [ "$updated_lines" -gt 10 ]; then
        cp "$TMPDIR/pt-todo-r${round}.txt" "$TODO_FILE"
        log "TODO 已更新 ($updated_lines 行, 来自 stdout 分隔符拆分)"
      fi
    else
      log "stdout 无分隔符，检查 Planner 是否直接写了文件..."
      cp "$TMPDIR/pt-full-r${round}.txt" "$TMPDIR/pt-resp-r${round}.txt"
    fi

    local todo_mtime_after; todo_mtime_after=$(stat -f %m "$TODO_FILE" 2>/dev/null || echo 0)
    if [ "$todo_mtime_after" != "$todo_mtime_before" ] && [ ! -s "$TMPDIR/pt-todo-r${round}.txt" ]; then
      local file_lines; file_lines=$(wc -l < "$TODO_FILE" | tr -d ' ')
      log "TODO 已由 Planner 直接写入文件 ($file_lines 行)"
    fi

    append_log "Phase 2 Round ${round}: Planner TODO 回应" "$TMPDIR/pt-resp-r${round}.txt"
  done

  log "TODO 完成: $TODO_FILE (经过 $todo_round_count 轮评审)"
}

# ============================================================
# Main
# ============================================================
main() {
  if [ ! -x "$CURSOR_AGENT" ]; then
    echo "错误: cursor-agent 未找到: $CURSOR_AGENT" >&2
    echo "请运行: curl https://cursor.com/install -fsS | bash" >&2
    exit 1
  fi

  log "=========================================="
  log "Multi-Agent Plan: ${TOPIC}"
  log "Mode: ${MODE}"
  log "Planner: ${PLANNER_MODEL}"
  log "Reviewer: ${REVIEWER_MODEL}"
  log "Project: ${PROJECT_ROOT}"
  log "Plan: ${PLAN_FILE}"
  log "TODO: ${TODO_FILE}"
  log "Discussion: ${DISCUSSION}"
  log "=========================================="

  phase1
  phase2

  # Final summary → stdout (for caller to capture)
  cat <<SUMMARY

=== Multi-Agent Plan 完成 ===
Plan:       ${PLAN_FILE}
TODO:       ${TODO_FILE}
Discussion: ${DISCUSSION}
Planner:    ${PLANNER_MODEL}
Reviewer:   ${REVIEWER_MODEL}
SUMMARY

  log "=== 全部完成 ==="
}

main
