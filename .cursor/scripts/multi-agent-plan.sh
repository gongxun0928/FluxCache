#!/usr/bin/env bash
# 包装器：转发到 skill 内脚本（支持项目安装与全局安装）
# 用法: bash .cursor/scripts/multi-agent-plan.sh <topic> <requirements_file>
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

RUNNER=""
if [ -f "${PROJECT_ROOT}/.cursor/skills/multi-agent-design-review/scripts/multi-agent-plan.sh" ]; then
  RUNNER="${PROJECT_ROOT}/.cursor/skills/multi-agent-design-review/scripts/multi-agent-plan.sh"
elif [ -f "${HOME}/.cursor/skills/multi-agent-design-review/scripts/multi-agent-plan.sh" ]; then
  RUNNER="${HOME}/.cursor/skills/multi-agent-design-review/scripts/multi-agent-plan.sh"
else
  echo "错误: multi-agent-plan 脚本未找到。请安装 multi-agent-design-review skill 到 .cursor/skills/ 或 ~/.cursor/skills/" >&2
  exit 1
fi

exec bash "$RUNNER" "$@"
