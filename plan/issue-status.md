# FluxCache Issue 状态追踪

> 本文件集中管理当前 roadmap 中的活跃 issue 状态，由 issue-tracker skill 维护。  
> 历史大 issue `P1-05 / P1-08 / P1-14 / P1-15` 已拆分，不再作为活跃状态项跟踪。

## 使用约定

- 当前批量处理、拓扑排序和依赖解除，均以本文件为准。
- 旧的 `P1/P2/P3` 前缀只保留为文件编号兼容，不代表当前执行阶段。
- 若用户提到旧大 issue，应映射到对应子 issue：
  - `P1-05 -> P1-05A/B/C`
  - `P1-08 -> P1-08A/B`
  - `P1-14 -> P1-14A/B/C/D`
  - `P1-15 -> P1-15A/B/C/D`

## 状态机

```
created ──→ in_progress ──→ pr_review ──→ pr_merged ──→ completed
              │                │
              └── created ◄────┘  (打回 / 回退)
```

| 状态 | 含义 |
|---|---|
| `created` | 已创建，未开始 |
| `in_progress` | 正在编码实现 |
| `pr_review` | PR 已提交，等待审查 |
| `pr_merged` | PR 已合并 |
| `completed` | 已完成并验证 |

## 依赖解除规则

- 依赖项状态为 **`completed`** 或 **`pr_merged`** 时，视为已解除。
- 依赖项状态为 `created`、`in_progress`、`pr_review` 时，视为阻塞。

## 阶段摘要

| Phase | 含义 | 批处理建议 |
|---|---|---|
| `Phase A` | 契约冻结 | 可整体批处理 |
| `Phase B` | 元数据面闭环 | 建议按 `P1-05*`、`P1-08*` 分组 |
| `Phase C` | 数据面闭环 | 建议先读路径，再写路径 |
| `Phase D` | 恢复与删除语义 | 恢复与 GC 建议分开执行 |
| `Phase E` | 多 Worker 与传输增强 | 单独小批次推进 |
| `Phase F` | 多层缓存与淘汰 | 先策略接口，再流程 |
| `Phase G` | 访问入口 | SDK 优先，FUSE 后置 |
| `Phase H` | 后端扩展 | S3/HDFS 分开执行 |
| `Phase I` | HA 与弹性 | 高风险，避免混批 |
| `Phase J` | 性能、可观测与质量 | 以专项小批次推进 |

## Phase A：契约冻结

| Issue | 标题 | 分级 | 依赖 | 状态 | PR | 备注 |
|---|---|---|---|---|---|---|
| P1-01 | 项目脚手架与构建系统 | P1 | 无 | completed | | 2026-03-11 实现并验证 |
| P1-16 | 核心类型定义 | P1 | P1-01 | completed | | 2026-03-11 实现并验证 |
| P1-02 | 配置加载系统（YAML） | P1 | P1-01 | completed | | 2026-03-11 实现并验证 |
| P1-11 | UFS 抽象层与 LocalFS 驱动 | P1 | P1-01 | completed | | 2026-03-11 实现并验证 |
| P1-03 | Phase 1 最小 RPC 契约 | P1 | P1-01, P1-16, P1-11 | completed | | 2026-03-11 实现并验证 |
| P1-04 | ChannelPool 最小实现 | P1 | P1-03 | completed | | 2026-03-11 实现并验证 |

## Phase B：元数据面闭环

| Issue | 标题 | 分级 | 依赖 | 状态 | PR | 备注 |
|---|---|---|---|---|---|---|
| P1-05A | Master 进程启动与服务注册骨架 | P1 | P1-02, P1-03, P1-16 | completed | | 2026-03-11 实现并验证 |
| P1-05B | InodeStore 与 InodeTree 最小持久化 | P1 | P1-05A, P1-11 | completed | | 2026-03-11 实现并验证 |
| P1-05C | WorkerManager 与 HashRingManager 最小实现 | P1 | P1-05A, P1-16 | completed | | 2026-03-11 实现并验证 |
| P1-08A | MountTable 核心映射与 RPC | P1 | P1-05A, P1-11 | completed | | 2026-03-11 实现并验证 |
| P1-08B | SyncFromUfs 与 Unmount 安全规则 | P1 | P1-05B, P1-08A, P1-11 | completed | | 2026-03-11 实现并验证 |

## Phase C：数据面闭环

| Issue | 标题 | 分级 | 依赖 | 状态 | PR | 备注 |
|---|---|---|---|---|---|---|
| P1-06 | Worker 进程骨架与心跳 | P1 | P1-02, P1-03, P1-16 | completed | | 2026-03-11 实现并验证 |
| P1-09 | Memory Tier 基础能力 | P1 | P1-06 | completed | | 2026-03-11 实现并验证 |
| P1-10 | PageStore 最小实现 | P1 | P1-09, P1-16 | completed | | 2026-03-11 实现并验证 |
| P1-07 | Client RPC 封装与本地路由缓存骨架 | P1 | P1-02, P1-04, P1-16 | completed | | 2026-03-11 实现并验证 |
| P1-14A | Master.GetFileInfo 最小实现 | P1 | P1-05B, P1-05C, P1-08B, P1-11 | completed | | 2026-03-11 实现并验证 |
| P1-14B | Worker.ReadPages 最小实现 | P1 | P1-06, P1-10, P1-11 | completed | | 2026-03-11 实现并验证 |
| P1-14C | Client.Read 最小实现 | P1 | P1-07, P1-14A, P1-14B, P1-16 | completed | | 2026-03-11 实现并验证 |
| P1-14D | 读路径端到端验证 | P1 | P1-14C | completed | | 2026-03-11 实现并验证 |
| P1-15A | Master.CreateFile / CompleteFile | P1 | P1-05B, P1-08B | completed | | 2026-03-11 实现并验证 |
| P1-15B | Worker.WritePages 最小实现 | P1 | P1-10, P1-11, P1-14B | completed | | 2026-03-11 实现并验证 |
| P1-15C | Client.Write 最小实现 | P1 | P1-07, P1-15A, P1-15B, P1-16 | completed | | 2026-03-11 实现并验证 |
| P1-15D | 写路径端到端验证 | P1 | P1-14D, P1-15C | completed | | 2026-03-11 实现并验证 | |

## Phase D：恢复与删除语义

| Issue | 标题 | 分级 | 依赖 | 状态 | PR | 备注 |
|---|---|---|---|---|---|---|
| P2-01 | SSD / HDD Tier 扩展 | P1 | P1-09 | completed | | 2026-03-11 实现并验证 |
| P2-05 | MetaStore RocksDB 集成与恢复 | P1 | P2-01, P1-10 | completed | | 2026-03-11 实现并验证 |
| P2-05B | Worker GC 与 orphan / misplaced block 对账 | P1 | P2-05, P1-05C, P1-15D | completed | | 2026-03-11 实现并验证 |

## Phase E：多 Worker 与传输增强

| Issue | 标题 | 分级 | 依赖 | 状态 | PR | 备注 |
|---|---|---|---|---|---|---|
| P2-08 | ChannelPool 完整实现与幂等重试边界 | P1 | P1-04, P1-14D, P1-15D | completed | | 2026-03-11 实现并验证 |

## Phase F：多层缓存与淘汰

| Issue | 标题 | 分级 | 依赖 | 状态 | PR | 备注 |
|---|---|---|---|---|---|---|
| P2-03 | 淘汰策略接口与 LRU 实现 | P1 | P1-10 | completed | | 2026-03-11 实现并验证 |
| P2-02 | 自动层级晋升与淘汰流程 | P1 | P2-01, P2-03, P2-05 | completed | | 2026-03-11 实现并验证 |
| P2-04 | LFU 淘汰策略 | P1 | P2-03, P2-02 | completed | | 2026-03-11 实现并验证 |

## Phase G：访问入口

| Issue | 标题 | 分级 | 依赖 | 状态 | PR | 备注 |
|---|---|---|---|---|---|---|
| P1-12 | CLI smoke 工具 | P2 | P1-14D, P1-15D | completed | | 2026-03-11 实现并验证 |
| P2-09 | C++ SDK MVP（不含 rename / 目录变更） | P1 | P1-14D, P1-15D, P2-08 | completed | | 2026-03-11 实现并验证 |
| P2-11 | Client 本地 Page 内存缓存 | P1 | P2-09, P1-16 | completed | | 2026-03-11 实现并验证 |
| P2-06 | FUSE 挂载基础能力 | P1 | P2-09 | completed | | 2026-03-11 实现并验证 |

## Phase H：后端扩展

| Issue | 标题 | 分级 | 依赖 | 状态 | PR | 备注 |
|---|---|---|---|---|---|---|
| P2-07 | S3 UFS 驱动 | P1 | P1-11 | created | | |
| P3-01 | HDFS UFS 驱动 | P1 | P1-11 | created | | |

## Phase I：HA 与弹性

| Issue | 标题 | 分级 | 依赖 | 状态 | PR | 备注 |
|---|---|---|---|---|---|---|
| P2-10 | HA Journal / Raft 设计 Spike | P1 | P1-05B, P1-08B, P1-15D | completed | | 2026-03-11 设计完成 |
| P3-05 | 弹性配置层（超时 / 重试 / 熔断） | P1 | P2-08 | completed | | 2026-03-11 实现并验证 |
| P3-06 | 安全恢复与受限降级策略 | P0 | P2-05B, P3-05, P2-10 | created | | |

## Phase J：性能、可观测与质量

| Issue | 标题 | 分级 | 依赖 | 状态 | PR | 备注 |
|---|---|---|---|---|---|---|
| P1-13 | Metrics 基础导出 | P2 | P1-05A, P1-06 | completed | | 2026-03-11 实现并验证 |
| P3-02 | PageStore 并发优化 | P1 | P1-10 | completed | | 2026-03-11 实现并验证 |
| P3-03 | 批量 RPC 与顺序读 pipeline | P1 | P1-14D, P2-08 | created | | |
| P3-04 | 零拷贝与减少复制路径调研/落地 | P1 | P1-15D, P3-03 | created | | |
| P3-08 | 可观测性指标扩展 | P2 | P1-13, P2-02, P3-05 | created | | |
| P3-09 | 慢请求与热点页追踪 | P2 | P3-08 | created | | |
| P3-07 | 缺陷治理与回归测试轨道 | P2 | P1-15D | completed | | 2026-03-11 实现并验证 |
