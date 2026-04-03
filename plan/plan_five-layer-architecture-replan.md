# Plan: 五层架构重规划

> Status: Draft

## Goal

基于当前 `Client -> Master -> Worker -> UFS` 原型，收敛并冻结面向 `PD / MetaService / BlockNode / Scheduler / TaskNode` 的演进设计，优先解决职责边界、SoT、最小闭环和迁移顺序问题，为后续 issue 细化和实现提供稳定基线。

## 设计依据

- [design/five-layer-architecture-design.md](../design/five-layer-architecture-design.md)
- [design/metadata-design.md](../design/metadata-design.md)
- [design/ha-journal-design.md](../design/ha-journal-design.md)
- [plan/fluxcache-feature-roadmap.md](./fluxcache-feature-roadmap.md)
- [plan/issue-status.md](./issue-status.md)

## 变更分级

P1 — 跨 3+ 模块的架构重规划，涉及未来接口边界、服务拆分与 issue 组织方式调整。

## Steps

1. **冻结边界与 SoT**
   - 确认 PD、MetaService、BlockNode、Scheduler、TaskNode 的职责边界；
   - 确认全局配置、挂载表、节点成员、namespace、block location、任务状态等核心状态的唯一 SoT；
   - 明确 `file_version`、`block_map_version`、`placement_epoch` 的语义分工。

2. **冻结最小闭环**
   - 定义最小读路径；
   - 定义最小写路径（继续保持 `write-through`）；
   - 定义最小任务闭环（首批仅 `Prewarm` 与 `ReconcileGc`）。

3. **冻结迁移策略**
   - 明确“先逻辑拆分，再进程拆分，再补齐能力，最后产品化”的阶段顺序；
   - 明确旧 `Master / Worker` 到新五层的逻辑映射关系；
   - 明确兼容层由 SDK / Gateway 承担多跳编排。

4. **冻结 issue 组织方式**
   - 先按工作组（WG-00 ~ WG-05）定义 issue 分组原则；
   - 暂不直接铺开几十个细粒度 issue；
   - 后续基于设计和工作组再生成详细 DAG。

5. **形成后续评审与验收基线**
   - 将本轮设计文档作为后续 issue 评审的设计基线；
   - 后续细化 issue 时，必须引用本文档中的边界、SoT、最小闭环和迁移原则。

## 文件路径

| 文件 | 操作 |
|------|------|
| `design/five-layer-architecture-design.md` | 新增 |
| `plan/plan_five-layer-architecture-replan.md` | 新增 |
| `plan/todo-five-layer-architecture-replan.md` | 新增 |

## 验证动作

- 文档自审：设计边界、SoT、最小闭环、迁移阶段、issue 分组原则之间无自相矛盾；
- 依赖检查：不再沿用“单 Master 统管一切”的默认假设；
- 评审标准：能够回答“谁是 SoT、谁负责执行、谁负责编排、最小闭环如何验证”。

## Risks

1. **PD 技术路线尚未冻结**
   - 语义上参考 TiKV PD，但实现上是否 embed etcd 需后续专项设计。

2. **OpenDAL 方向尚未落地**
   - 本轮只冻结能力模型与迁移方向，不承诺第一阶段即替换现有 UFS 抽象。

3. **MetaEngine 外部 KV 适配仍有事务语义风险**
   - 当前仅冻结接口方向，不承诺第一阶段支持 KinsDB / TiKV / FDB。

4. **兼容期 SDK / Gateway 复杂度会上升**
   - 迁移期间由客户端承担多跳编排，会引入额外适配成本。

## To Confirm

1. 挂载表是否长期保留在 PD，还是后续拆成“全局挂载策略”和“租户视图”两层；
2. 第一阶段的 BlockNode 是否继续采用默认单 owner，不立即做多副本；
3. Scheduler 的首批 northbound API 是否先以内部 RPC 为主，REST API 后置；
4. 是否将五层架构相关 issue 统一归入新的 Phase/WG，而不是继续平铺进现有 A~K 阶段。
