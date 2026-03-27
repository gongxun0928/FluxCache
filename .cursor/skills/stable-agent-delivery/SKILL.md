---
name: stable-agent-delivery
description: 可复用的 Agent 全流程稳定交付技能。覆盖 design → plan → todo → coding → review → testing（不含部署）。当用户要求端到端稳定交付、分级治理或多阶段门禁时启用，并遵循仓库 rules 执行。
---

# Stable Agent Delivery

## 触发条件

- 用户要求端到端稳定交付；
- 用户要求按 P0/P1/P2 分级治理；
- 用户要求设计、编码、评审、测试全流程闭环；
- 用户要求多阶段门禁、阶段放行或 reviewer 参与。

## 使用方式

1. 先按 `change-tiering-and-qg.mdc` 判定变更等级。
2. 全流程门禁、reviewer 分级策略、放行/回退规则统一遵循 `agent-lifecycle-gates.mdc`。
3. Review 输出格式统一遵循 `review-evidence-checklist.mdc`。
4. Testing 执行与失败重试统一遵循 `testing-gate-no-deploy.mdc`。
5. Coding 阶段按 `test-driven-development` skill 执行：先失败测试，再最小实现，再重构。

## 输出

- 更新后的代码、测试和相关文档；
- 对应的 `plan/plan_*.md` 与 `plan/todo-*.md`；
- 最终评审与测试结果；
- 最终变更报告。
