#!/usr/bin/env bash
# 多 Agent 设计评审：GPT-5.3 Codex + Claude Opus 4.6 并行评审
#
# 用法: bash <script_path> [设计文档路径]
#   - 项目安装: bash .cursor/skills/multi-agent-design-review/scripts/multi-agent-review.sh ...
#   - 全局安装: bash ~/.cursor/skills/multi-agent-design-review/scripts/multi-agent-review.sh ...
# 示例: bash .../multi-agent-review.sh design/metadata-design.md

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 双模式 PROJECT_ROOT 检测（与 multi-agent-plan.sh 一致）
CANDIDATE_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
if [[ "$CANDIDATE_ROOT" == "$HOME" ]] || [[ "$CANDIDATE_ROOT" == "$HOME/.cursor" ]]; then
  PROJECT_ROOT="${WORKSPACE:-$PWD}"
else
  PROJECT_ROOT="$CANDIDATE_ROOT"
fi

CURSOR_AGENT="${HOME}/.local/bin/cursor-agent"
OUTPUT_DIR="${PROJECT_ROOT}/design/design-review-sessions"
TIMESTAMP=$(date +%Y%m%d-%H%M)

DESIGN_DOC="${1:-design/metadata-design.md}"
DESIGN_ABS="${PROJECT_ROOT}/${DESIGN_DOC}"

if [ ! -f "$DESIGN_ABS" ]; then
  echo "错误: 设计文档不存在: $DESIGN_ABS"
  exit 1
fi

if [ ! -x "$CURSOR_AGENT" ]; then
  echo "错误: cursor-agent 未找到: $CURSOR_AGENT"
  echo "请运行: curl https://cursor.com/install -fsS | bash"
  exit 1
fi

# reviewer-design.md 查找顺序：项目 > 技能内嵌 > 全局
SKILL_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REVIEWER_TASK=""
for path in \
  "${PROJECT_ROOT}/.cursor/agents/reviewer-design.md" \
  "${SKILL_DIR}/agents/reviewer-design.md" \
  "${HOME}/.cursor/agents/reviewer-design.md"; do
  if [ -f "$path" ]; then
    REVIEWER_TASK=$(cat "$path")
    break
  fi
done
if [ -z "$REVIEWER_TASK" ]; then
  echo "错误: reviewer-design.md 未找到（请安装到项目 .cursor/agents/ 或 ~/.cursor/agents/ 或技能目录 agents/）"
  exit 1
fi

DESIGN_CONTENT=$(cat "$DESIGN_ABS")

PROMPT="[System Context]
${REVIEWER_TASK}

[User Prompt]
请评审以下设计文档: ${DESIGN_DOC}

---
${DESIGN_CONTENT}
---

输出完整评审，格式见 Output Format。末尾注明当前模型名称。"

echo "=== 多 Agent 设计评审 ==="
echo "设计文档: $DESIGN_DOC"
echo "模型: gpt-5.3-codex + opus-4.6-thinking"
echo "输出目录: $OUTPUT_DIR"
echo ""

mkdir -p "$OUTPUT_DIR"
cd "$PROJECT_ROOT"

OUTPUT_PREFIX="${OUTPUT_DIR}/review-${TIMESTAMP}"
GPT_OUTPUT="${OUTPUT_PREFIX}-gpt53codex.txt"
OPUS_OUTPUT="${OUTPUT_PREFIX}-opus46.txt"
GPT_PID=""
OPUS_PID=""

run_reviewer() {
  local name=$1
  local model=$2
  local output_file=$3
  echo "[$name] 启动评审 (model: $model)..."
  if "$CURSOR_AGENT" --model "$model" --output-format text --trust --force -p "$PROMPT" > "$output_file" 2>"${output_file}.err"; then
    local lines=$(wc -l < "$output_file" | tr -d ' ')
    echo "[$name] 完成 ($lines 行) -> $output_file"
  else
    echo "[$name] 失败 (exit=$?)"
    if [ -s "${output_file}.err" ]; then
      echo "[$name] 错误信息:"
      head -10 "${output_file}.err"
    fi
    return 1
  fi
}

run_reviewer "GPT-5.3-Codex" "gpt-5.3-codex" "$GPT_OUTPUT" &
GPT_PID=$!

run_reviewer "Opus-4.6" "opus-4.6-thinking" "$OPUS_OUTPUT" &
OPUS_PID=$!

echo "等待两个 Reviewer 完成 (PID: $GPT_PID, $OPUS_PID)..."
echo ""

FAIL=0
wait $GPT_PID || FAIL=$((FAIL + 1))
wait $OPUS_PID || FAIL=$((FAIL + 1))

echo ""
echo "=== 评审完成 ==="
echo "GPT-5.3 Codex: $GPT_OUTPUT"
echo "Opus 4.6:      $OPUS_OUTPUT"

if [ $FAIL -gt 0 ]; then
  echo "警告: $FAIL 个 Reviewer 执行失败"
  exit 1
fi

for f in "$GPT_OUTPUT" "$OPUS_OUTPUT"; do
  if [ ! -s "$f" ]; then
    echo "警告: 输出为空: $f"
    FAIL=$((FAIL + 1))
  fi
done

if [ $FAIL -gt 0 ]; then
  echo "存在空输出，请检查 .err 文件"
  exit 1
fi

echo "两个 Reviewer 均成功完成"
