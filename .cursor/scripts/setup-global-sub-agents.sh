#!/usr/bin/env bash
# 全局激活多 Agent 设计评审：复制 agents、agents-settings、Skill 到 ~/.cursor/
# 运行: bash .cursor/scripts/setup-global-sub-agents.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
AGENTS_SRC="$PROJECT_ROOT/.cursor/agents"
AGENTS_SETTINGS_SRC="$PROJECT_ROOT/.cursor/agents-settings"
SKILL_SRC="$PROJECT_ROOT/.cursor/skills/multi-agent-design-review"
GLOBAL_AGENTS="$HOME/.cursor/agents"
GLOBAL_AGENTS_SETTINGS="$HOME/.cursor/agents-settings"
GLOBAL_SKILLS="$HOME/.cursor/skills/multi-agent-design-review"
GLOBAL_MCP="$HOME/.cursor/mcp.json"

echo "=== 全局激活多 Agent 设计评审 ==="
echo "项目根目录: $PROJECT_ROOT"
echo ""

# 1. 复制 agent 定义
mkdir -p "$GLOBAL_AGENTS"
cp -v "$AGENTS_SRC"/*.md "$GLOBAL_AGENTS/"
echo "[1/4] 已复制 agent 定义到 $GLOBAL_AGENTS"

# 2. 复制 agents-settings（多模型配置）
mkdir -p "$GLOBAL_AGENTS_SETTINGS"
cp -rv "$AGENTS_SETTINGS_SRC"/* "$GLOBAL_AGENTS_SETTINGS/" 2>/dev/null || true
echo "[2/4] 已复制 agents-settings 到 $GLOBAL_AGENTS_SETTINGS"

# 3. 复制 Skill 到全局（所有项目可用）
mkdir -p "$GLOBAL_SKILLS"
cp -v "$SKILL_SRC"/SKILL.md "$GLOBAL_SKILLS/" 2>/dev/null || true
echo "[3/4] 已复制 multi-agent-design-review Skill 到 $GLOBAL_SKILLS"

# 4. 生成 mcp.json 配置（若不存在）
USER_HOME="${HOME}"
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
  echo "[4/4] 已创建 $GLOBAL_MCP（3 种 Cursor 模型）"
else
  echo "[4/4] $GLOBAL_MCP 已存在，请手动添加 sub-agents-auto / gpt5.3-codex / opus4.6"
  echo "      参考 MCP 文档手动添加 sub-agents 配置"
fi

echo ""
echo "=== 全局激活完成 ==="
echo ""
echo "请确保："
echo "  1. cursor-agent 已安装: which cursor-agent"
echo "  2. 已登录: cursor-agent login"
echo "  3. 验证模型: cursor-agent models（若 model ID 不符，编辑 agents-settings/*/cli-config.json）"
echo "  4. 重启 Cursor"
echo ""
echo "详细说明见 Cursor MCP 文档"
