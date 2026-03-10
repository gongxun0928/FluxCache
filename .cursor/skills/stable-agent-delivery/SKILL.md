---
name: stable-agent-delivery
description: 可复用的 Agent 全流程稳定交付技能。覆盖 design → plan → todo → coding → review → testing（不含部署），严格按阶段推进。
---

# Stable Agent Delivery

## 触发条件

- 用户要求端到端稳定交付；
- 用户要求按 P0/P1/P2 分级治理；
- 用户要求设计、编码、评审、测试全流程闭环。

## 输入

- 用户需求或任务卡；
- 相关设计文档、计划文档、todo 文档；
- 受影响代码与现有约束；
- 当前变更范围和目标文件路径。

## 执行步骤

1. 按 `change-tiering-and-qg.mdc` 判定变更等级。
2. 按 `agent-lifecycle-gates.mdc` 依次完成 Design、Plan、Todo、Coding、Review、Testing。
3. 使用本目录下模板生成任务卡、计划、Todo、评审记录、测试记录和 evidence。
4. Design、Plan、Todo 阶段产出文档后再进入实现。
5. Coding 完成后，按 `review-evidence-checklist.mdc` 形成评审产物。
6. 按 `testing-gate-no-deploy.mdc` 执行测试阶段，并记录结果。
7. 同步必要文档与状态回写，输出最终变更报告。

## 输出

- 更新后的代码、测试和相关文档；
- 对应的 `plan/plan_*.md` 与 `plan/todo-*.md`；
- 评审结论与测试结果；
- 最终变更报告（包含检查项结果与残余风险）。
