---
name: multi-agent-plan
description: Multi-agent technical plan design with fast/strict modes. Use when user asks for 多agent设计, 多agent方案, 设计技术方案, or architecture design with multi-agent collaboration.
---

# Multi-Agent Plan（多 Agent 协作设计）

## 触发条件

- 用户明确要求多 Agent 设计；
- 需要 Planner + Reviewer 协作产出技术方案；
- 跨模块、接口影响大或高风险设计需要额外评审。

## 输入

- 需求描述；
- 相关源码、设计文档和约束；
- 方案主题名 `<topic>`；
- 执行模式：`fast` 或 `strict`。

## 执行步骤

1. 将需求写入 `plan/requirements-<topic>.md`。
2. 选择模式：
   - `fast`：默认模式，适合中低风险任务。
   - `strict`：适合跨 3+ 模块、接口不兼容、核心路径改造等高风险任务。
3. 运行 `scripts/multi-agent-plan.sh <topic> plan/requirements-<topic>.md --mode <mode>`。
4. 监控脚本执行，等待 Planner 与 Reviewer 完成 Plan 和 Todo 的多轮收敛。
5. 读取生成的 `plan/plan_<topic>.md`、`plan/todo-<topic>.md` 和讨论记录。
6. 向用户汇总方案摘要、关键分歧和未决问题。

## 输出

- `plan/plan_<topic>.md`：最终技术方案；
- `plan/todo-<topic>.md`：对齐后的 TODO；
- `design/design-review-sessions/<topic>-discussion-*.md`：讨论记录；
- 面向用户的摘要说明。
