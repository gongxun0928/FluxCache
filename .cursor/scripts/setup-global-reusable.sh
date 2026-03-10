#!/usr/bin/env bash
# 将 .cursor 下通用可复用配置拷贝到 ~/.cursor/，使所有项目可用
#
# 用法: bash .cursor/scripts/setup-global-reusable.sh
#
# 拷贝内容：
#   - skills/multi-agent-design-review/  （完整目录，含 scripts、agents）
#   - agents/                            （Reviewer 角色定义）
#   - agents-settings/                   （多模型配置）
#   - rules/                             （通用规则：task-execution-loop, design-docs 等）
#   - mcp.json                           （若不存在则生成 sub-agents 配置）

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CURSOR_DIR="$PROJECT_ROOT/.cursor"
GLOBAL="$HOME/.cursor"
USER_HOME="$HOME"

echo "=== 拷贝通用 Cursor 配置到全局 ==="
echo "项目: $PROJECT_ROOT"
echo "目标: $GLOBAL"
echo ""

# 1. Skill（完整目录）
SRC_SKILL="$CURSOR_DIR/skills/multi-agent-design-review"
DST_SKILL="$GLOBAL/skills/multi-agent-design-review"
if [ -d "$SRC_SKILL" ]; then
  mkdir -p "$(dirname "$DST_SKILL")"
  rm -rf "$DST_SKILL"
  cp -r "$SRC_SKILL" "$DST_SKILL"
  echo "[1/5] 已复制 skills/multi-agent-design-review/ 到 $DST_SKILL"
else
  echo "[1/5] 跳过: $SRC_SKILL 不存在"
fi

# 2. Agents
SRC_AGENTS="$CURSOR_DIR/agents"
DST_AGENTS="$GLOBAL/agents"
if [ -d "$SRC_AGENTS" ]; then
  mkdir -p "$DST_AGENTS"
  cp -v "$SRC_AGENTS"/*.md "$DST_AGENTS/" 2>/dev/null || true
  echo "[2/5] 已复制 agents/ 到 $DST_AGENTS"
else
  echo "[2/5] 跳过: $SRC_AGENTS 不存在"
fi

# 3. Agents-settings
SRC_SETTINGS="$CURSOR_DIR/agents-settings"
DST_SETTINGS="$GLOBAL/agents-settings"
if [ -d "$SRC_SETTINGS" ]; then
  mkdir -p "$DST_SETTINGS"
  cp -r "$SRC_SETTINGS"/* "$DST_SETTINGS/" 2>/dev/null || true
  echo "[3/5] 已复制 agents-settings/ 到 $DST_SETTINGS"
else
  echo "[3/5] 跳过: $SRC_SETTINGS 不存在"
fi

# 4. 通用 Rules（不含 FluxCache 特定规则）
RULES_TO_COPY=(
  "task-execution-loop.mdc"
  "design-docs.mdc"
  "documentation.mdc"
  "testing-conventions.mdc"
  "design-review-trigger.mdc"
)
DST_RULES="$GLOBAL/rules"
mkdir -p "$DST_RULES"
COPIED=0
for f in "${RULES_TO_COPY[@]}"; do
  if [ -f "$CURSOR_DIR/rules/$f" ]; then
    cp -v "$CURSOR_DIR/rules/$f" "$DST_RULES/"
    COPIED=$((COPIED + 1))
  fi
done
echo "[4/5] 已复制 $COPIED 个通用 rules 到 $DST_RULES"

# 5. mcp.json（若不存在则生成）
GLOBAL_MCP="$GLOBAL/mcp.json"
if [ ! -f "$GLOBAL_MCP" ]; then
  cat > "$GLOBAL_MCP" << EOF
{
  "mcpServers": {
    "sub-agents-auto": {
      "command": "npx",
      "args": ["-y", "sub-agents-mcp"],
      "env": {
        "AGENTS_DIR": "$USER_HOME/.cursor/agents",
        "AGENT_TYPE": "cursor",
        "AGENTS_SETTINGS_PATH": "$USER_HOME/.cursor/agents-settings/auto",
        "PATH": "$USER_HOME/.local/bin:/usr/local/bin:/usr/bin:/bin"
      }
    },
    "sub-agents-gpt5.3-codex": {
      "command": "npx",
      "args": ["-y", "sub-agents-mcp"],
      "env": {
        "AGENTS_DIR": "$USER_HOME/.cursor/agents",
        "AGENT_TYPE": "cursor",
        "AGENTS_SETTINGS_PATH": "$USER_HOME/.cursor/agents-settings/gpt5.3-codex",
        "PATH": "$USER_HOME/.local/bin:/usr/local/bin:/usr/bin:/bin"
      }
    },
    "sub-agents-opus4.6": {
      "command": "npx",
      "args": ["-y", "sub-agents-mcp"],
      "env": {
        "AGENTS_DIR": "$USER_HOME/.cursor/agents",
        "AGENT_TYPE": "cursor",
        "AGENTS_SETTINGS_PATH": "$USER_HOME/.cursor/agents-settings/opus4.6",
        "PATH": "$USER_HOME/.local/bin:/usr/local/bin:/usr/bin:/bin"
      }
    }
  }
}
EOF
  echo "[5/5] 已创建 $GLOBAL_MCP（sub-agents MCP 配置）"
else
  echo "[5/5] $GLOBAL_MCP 已存在，跳过"
fi

echo ""
echo "=== 完成 ==="
echo ""
echo "已拷贝到 ~/.cursor/："
echo "  - skills/multi-agent-design-review/  （多 Agent 设计评审）"
echo "  - agents/                            （Reviewer 定义）"
echo "  - agents-settings/                   （多模型配置）"
echo "  - rules/                             （5 个通用规则）"
echo "  - mcp.json                           （若为新创建）"
echo ""
echo "后续步骤："
echo "  1. cursor-agent 已安装: which cursor-agent"
echo "  2. 已登录: cursor-agent login"
echo "  3. 验证模型: cursor-agent models"
echo "  4. 重启 Cursor 使配置生效"
echo ""
echo "说明: hooks（on-stop.sh）为项目级配置，未拷贝到全局。"
echo "      新项目需单独拷贝 .cursor/hooks/ 和 .cursor/scripts/ 到项目内。"
