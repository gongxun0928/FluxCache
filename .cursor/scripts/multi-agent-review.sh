#!/usr/bin/env bash
# 包装器：转发到 skill 内脚本（支持项目安装与全局安装）
# 用法: bash .cursor/scripts/multi-agent-review.sh [设计文档路径]
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

RUNNER=""
if [ -f "${PROJECT_ROOT}/.cursor/skills/multi-agent-design-review/scripts/multi-agent-review.sh" ]; then
  RUNNER="${PROJECT_ROOT}/.cursor/skills/multi-agent-design-review/scripts/multi-agent-review.sh"
elif [ -f "${HOME}/.cursor/skills/multi-agent-design-review/scripts/multi-agent-review.sh" ]; then
  RUNNER="${HOME}/.cursor/skills/multi-agent-design-review/scripts/multi-agent-review.sh"
else
  echo "错误: multi-agent-review 脚本未找到。请安装 multi-agent-design-review skill 到 .cursor/skills/ 或 ~/.cursor/skills/" >&2
  exit 1
fi

exec bash "$RUNNER" "$@"
