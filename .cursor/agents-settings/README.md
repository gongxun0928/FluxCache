# Cursor 多模型 Reviewer 配置

本目录为 sub-agents-mcp 的 `AGENTS_SETTINGS_PATH` 提供不同模型的 Cursor CLI 配置。

## 目录结构

| 目录 | 模型 | 用途 |
|------|------|------|
| `auto/` | auto | Reviewer 1：自动选择 |
| `gpt5.3-codex/` | GPT-5.3 Codex | Reviewer 2 |
| `opus4.6/` | Claude Opus 4.6 | Reviewer 3 |

## 验证模型 ID

运行以下命令获取 Cursor 当前支持的模型列表：

```bash
cursor-agent models
# 或
cursor-agent --list-models
```

若配置中的 model 值与实际不符，编辑对应目录下的 `cli-config.json`，将 `model` 字段改为正确的 ID。

## 模型 ID（已验证）

当前配置使用的模型 ID 已与 `cursor-agent models` 输出核对：

- `auto` — Auto
- `gpt-5.3-codex` — GPT-5.3 Codex
- `opus-4.6` — Claude 4.6 Opus

若 Cursor 更新模型列表，可运行 `cursor-agent models` 查看最新 ID 并相应修改 `cli-config.json`。

## 复制到全局目录

配置 MCP 时，`AGENTS_SETTINGS_PATH` 需使用**绝对路径**。将本目录复制到用户目录：

```bash
cp -r /Users/gongxun/workspace/code/FluxCache/.cursor/agents-settings ~/.cursor/
```

然后 MCP 配置中使用 `AGENTS_SETTINGS_PATH: "/Users/gongxun/.cursor/agents-settings/auto"` 等。
