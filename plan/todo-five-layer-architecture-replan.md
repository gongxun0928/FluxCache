# TODO

> Status: Draft

## Items

### 1. [ ] 冻结五层边界与 SoT 表

- Files: `design/five-layer-architecture-design.md`, `issues/index.md`, `plan/issue-status.md`
- DoD:
  - [ ] PD / MetaService / BlockNode / Scheduler / TaskNode 的职责边界固定
  - [ ] SoT 表固定，且不再存在“同一状态双源”描述
  - [ ] 旧 Master / Worker 语义到新五层组件的映射写清楚

### 2. [ ] 生成基于工作组的 issue 骨架

- Files: `issues/index.md`, `issues/*.md`, `plan/issue-status.md`
- DoD:
  - [ ] 基于 WG-00 ~ WG-05 生成新的 issue 分组
  - [ ] 每个 issue 都有明确依赖与可验证 DoD
  - [ ] 最小闭环 issue 与横切能力 issue 的顺序明确

### 3. [ ] 定义最小五层 E2E 验收剧本

- Files: `design/five-layer-architecture-design.md`, `plan/plan_five-layer-architecture-replan.md`
- DoD:
  - [ ] 最小读路径、写路径、任务路径均有文字化剧本
  - [ ] 剧本中的参与方、输入输出、版本字段明确
  - [ ] 可直接映射为后续测试和集成验证 issue

### 4. [ ] 设计逻辑拆分与兼容层

- Files: `design/five-layer-architecture-design.md`, `plan/plan_five-layer-architecture-replan.md`
- DoD:
  - [ ] 说明如何从现有 Master / Worker 代码逻辑拆分到五层模块
  - [ ] 说明迁移期 SDK / Gateway 的兼容职责
  - [ ] 明确哪些旧路径在迁移期保留、哪些新路径必须优先落地

### 5. [ ] 设计阶段自审与多方 review

- Files: `design/five-layer-architecture-design.md`, `plan/plan_five-layer-architecture-replan.md`, `plan/todo-five-layer-architecture-replan.md`
- DoD:
  - [ ] Review 结论明确“通过 / 需修订 / 有条件通过”
  - [ ] 风险、未决问题、测试缺口被单独列出
  - [ ] 后续进入 coding 前的阻断项已列清
