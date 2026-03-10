# Multi-Agent Design Review Skill

Planner + Reviewer 双模型协作设计技术方案，迭代至共识后输出 Plan 与 TODO。

## 安装

### 方式一：全局安装（所有项目生效）

```bash
cp -r multi-agent-design-review ~/.cursor/skills/
```

### 方式二：项目安装（仅当前项目）

```bash
mkdir -p .cursor/skills
cp -r multi-agent-design-review .cursor/skills/
```

### 分享给他人

将整个 `multi-agent-design-review/` 目录拷贝给对方，对方按上述任一方式安装即可。

## 前置要求

- [cursor-agent](https://cursor.com/install) 已安装
- 项目有 `design/`、`plan/` 目录（design 存放设计文档，plan 存放 plan、todo，sessions 在 design/design-review-sessions）
- 建议项目有 `AGENTS.md` 定义 Plan 格式（可选）

## 目录结构

```
multi-agent-design-review/
├── SKILL.md           # 技能定义（Cursor 自动加载）
├── README.md          # 本说明
├── scripts/
│   ├── multi-agent-plan.sh    # 主流程：Plan + TODO 协作设计
│   └── multi-agent-review.sh  # 独立评审（仅评审已有设计文档）
└── agents/
    └── reviewer-design.md    # Reviewer 角色定义（可被项目覆盖）
```
