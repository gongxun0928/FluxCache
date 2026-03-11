# Agent 行为约束与知识地图

## 渐进式披露

1. **入口**：本文件 → `README.md`（架构与 MVP 边界）
2. **任务上下文**：`plan/issue-status.md`（状态真相源）→ `issues/<id>.md`（单 issue 详情）
3. **设计依据**：`design/*.md`（按需读取，如 metadata-design、block-id-and-file-layout）
4. **执行计划**：`plan/plan_*.md`、`plan/todo-*.md`（当前任务关联的 Plan/Todo）
5. **规范**：`.cursor/rules/*.mdc`（阶段门禁、评审、测试、代码风格）

## 知识目录

| 目录 | 用途 |
|------|------|
| `design/` | 设计文档（metadata、block-id、ufs、config 等） |
| `plan/` | 计划、Todo、issue 状态、roadmap |
| `issues/` | issue 需求与验收标准，依赖见 `index.md` |
| `.cursor/rules/` | 全流程约束（agent-lifecycle-gates、review-evidence-checklist 等） |
| `.cursor/skills/` | 稳定交付、多 Agent 评审、issue-tracker、TDD（Coding 阶段先写失败测试） |

## Git / Commit

- **Commit messages must be in English.** When creating, suggesting, or editing a git commit message, use English only (subject and body). This applies to both manual commit prompts and any commit message generated or revised by the agent.
