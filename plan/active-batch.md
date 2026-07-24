# 当前活跃批次

> 由 issue-tracker 维护。2026-07-23 文档对齐后更新。

## 批次目标

收尾 Phase K 残余项，并为下一阶段方向做决策准备。

## 执行顺序

1. **P5-05**：pjdfstest 实测基线（host 或 Docker 集成）并产出真实 pass/fail 报告
2. **可选残余**：FUSE 完整 truncate / 权限落盘（见 `docs/pjdfstest-baseline.md` Improving Pass Rate）
3. **方向决策（二选一，勿混批）**：
   - A. 继续当前架构：补生产缺口（CI、观测、多 Worker 运维、HA 重评）
   - B. 推进 draft PR #1 的五层架构重规划（设计冻结后再拆实现 issue）

## 当前状态

| Issue | 状态 | 备注 |
|-------|------|------|
| P5-01 | completed | PR #2 已合入 |
| P5-02 | completed | PR #2 已合入 |
| P5-03 | completed | Batch 1 完成；属性/truncate 为兼容 stub |
| P5-04 | completed | docker/ 已合入；建议在有 Docker 环境时重跑 e2e |
| P5-05 | created | 脚本+预期基线文档已有；缺实测报告与 Compose 集成 |
| P4-02 | suspended | NuRaft HA 暂停，勿与本批次混做 |

## 依赖解除

- P5-05 依赖：P5-03 ✓、P5-04 ✓（可开始）
