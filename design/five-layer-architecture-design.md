# 五层架构重规划设计

> 日期: 2026-04-03
> 状态: Draft
> 背景: 基于当前 `Client -> Master -> Worker -> UFS` 原型，重规划面向 `PD / MetaService / BlockNode / Scheduler / TaskNode` 的演进设计。

## 1. 目标与边界

本文档用于解决两类问题：

1. 复核上一轮 multi-agent review 的建议，明确哪些建议应采纳、哪些需要调整；
2. 将“未来五层架构”从口头目标收敛为可执行的设计基线，避免后续 issue 继续沿用 `Master / Worker` 的混合职责叙事。

本文档**不**尝试一次性重写整个系统，也**不**把所有远期能力都前置为当前阶段的实现目标。

### 1.1 本轮设计的输出目标

- 冻结五层组件的职责边界；
- 冻结关键状态的 source of truth（SoT）；
- 冻结迁移原则：先逻辑拆分，再进程拆分，再做高可用和生态扩展；
- 给出最小五层闭环的验收剧本；
- 给出后续 issue 规划应遵循的分层原则。

### 1.2 本轮设计的非目标

以下内容在本轮设计中只保留接口或里程碑位置，不作为第一波实现目标：

- 立即引入外部 KV（KinsDB / TiKV / FDB）；
- 立即把 UFS 全量替换为 OpenDAL 实现；
- 立即做 PD/Meta/Scheduler 的完整 HA 实现；
- 立即做 S3 Gateway、RDMA、多语言 SDK、KVCache 集成；
- 立即补全完整 POSIX/FUSE 能力。

## 2. 对上一轮建议的采纳与调整

### 2.1 直接采纳的建议

1. **先冻结层次与 SoT，再展开 issue。**
2. **必须把最小五层 E2E 闭环作为早期里程碑。**
3. **Scheduler / TaskNode 必须成为一等子系统，而不是继续内嵌在 Master / Worker 中。**
4. **HA 议题必须从“Master HA”改成“分层 HA”。**
5. **QoS / 多租户 / 观测必须按横切能力插入主线，而不是全部拖到最后。**

### 2.2 调整后的建议

有些建议方向正确，但不适合直接作为第一阶段实现动作，需要调整：

1. **PD 对标 TiKV PD 的语义，而不是立即对标具体实现。**  
   目标上，PD 应具备类似 TiKV PD 的一致性、拓扑和 placement 语义；  
   但在边界未冻结前，不应把“embed etcd”作为第一步实现阻塞项。

2. **BlockNode 的 UFS 抽象先对齐 OpenDAL 的 capability 模型，而不是立即替换当前 UFS 代码。**  
   第一阶段先统一抽象与能力描述，保留兼容层；  
   真正切换到 OpenDAL 或兼容 OpenDAL 的后端实现，放到后续阶段。

3. **Block location 必须成为一等元数据，但第一阶段允许保留“确定性单 owner”作为默认 placement 策略。**  
   这样可以避免在边界刚冻结时就把多副本、重平衡、迁移调度一次做完。

### 2.3 本轮明确拒绝的做法

1. **大爆炸重写**：直接废弃现有 Master / Worker 实现；
2. **在旧 `master.proto` / `worker.proto` 上继续堆未来 PD / Scheduler 语义；**
3. **在 BlockNode 未分界前继续把后台任务塞进 Worker 心跳路径；**
4. **在接口尚未收敛前推进完整 HA 或性能专项。**

## 3. 设计原则

### 3.1 一类状态只能有一个权威 SoT

未来系统中的每类核心状态必须只有一个权威真相源，禁止双源竞争：

- 节点成员与拓扑不能同时由 PD 和 MetaService 各自维护一份；
- 命名空间与 inode 不能同时由 PD 和 MetaService 各自维护一份；
- 任务状态不能同时由 Scheduler 和 TaskNode 各自维护一份；
- placement epoch、block map version、file version 的含义必须明确区分。

### 3.2 先逻辑拆分，再进程拆分

第一波实现应优先完成：

- 模块边界；
- 服务接口；
- 兼容层；
- 最小 E2E。

不要求第一天就把所有组件都拆成独立进程。若边界稳定前就强拆进程，调试和回归成本会显著上升。

### 3.3 先闭环，再扩展

所有后续 issue 都应先服务于一个可验证的最小闭环，再逐步扩展：

- 先跑通五层最小读/写/任务闭环；
- 再做 QoS、多租户、观测、HA；
- 最后再推进生态接入和高性能专项。

### 3.4 迁移期间由 SDK / Gateway 层承担多跳编排

五层架构下，请求路径会天然比当前多一跳或多几跳。迁移期间不应把跨服务编排逻辑塞回任一服务端组件，而应由：

- SDK；
- 后续的 Gateway / API Facade；

负责聚合调用，保证 SoT 不被重新混淆。

## 4. 目标架构

```text
                   +----------------------+
                   |         SDK          |
                   |  Gateway / Client    |
                   +----------+-----------+
                              |
        +---------------------+----------------------+
        |                                            |
        v                                            v
  +-------------+                              +-------------+
  |     PD      |                              |  Scheduler  |
  | config      |                              | task state  |
  | mounts      |                              | retry/cancel|
  | topology    |                              | dispatch    |
  | placement   |                              +------+------+
  +------+------+                                     |
         |                                            |
         v                                            v
  +-------------+                              +-------------+
  | MetaService |<---------------------------->|  TaskNode   |
  | inode       |                              | task steps  |
  | dentry      |                              | stateless   |
  | block map   |                              +------+------+
  | file ver    |                                     |
  +------+------+                                     |
         |                                            |
         +----------------------+---------------------+
                                |
                                v
                          +-------------+
                          |  BlockNode  |
                          | cache/tier  |
                          | UFS access  |
                          | local meta  |
                          +------+------+
                                 |
                                 v
                                UFS
```

## 5. 五层职责定义

### 5.1 PD（Placement Driver）

PD 是集群控制面的权威入口，负责：

- 全局配置、开关和集群级参数；
- 挂载表；
- MetaNode / BlockNode / TaskNode 的注册、存活、拓扑信息；
- placement policy；
- placement epoch；
- 节点能力视图（如容量、标签、可用区、角色）。

PD **不负责**：

- inode / dentry / block map；
- 文件版本；
- 任务状态机；
- block/page 读写。

### 5.2 MetaService Layer / MetaNode

MetaService 是元数据面的权威真相源，负责：

- namespace、inode、目录关系；
- block location / replica metadata；
- file version / block map version；
- 文件布局和逻辑块描述；
- 面向 SDK/Gateway 的元数据查询接口。

MetaService **不负责**：

- 节点成员管理；
- placement policy；
- 任务调度；
- 数据块实际存储。

MetaNode 是 MetaService 的服务化载体。它应优先支持：

- `EmbeddedRocksMetaEngine`：默认实现（**规划名称**，对应当前仓库里
  `InodeTree + InodeStore(RocksDB)` 的演进形态，并非现有代码中的正式类名）；
- 可插拔 MetaEngine 接口；
- 未来接 KinsDB / TiKV / FDB / CubeFS MetaNode。

### 5.3 BlockNode

BlockNode 是数据面节点，负责：

- block/page 的存储与缓存；
- tier 管理（MEM / SSD / HDD）；
- promotion / eviction；
- 本地恢复元数据；
- UFS 读写与缓存回填；
- 对外提供读写 RPC。

BlockNode **不负责**：

- 全局 placement 决策；
- 任务编排；
- inode / namespace 维护。

### 5.4 Scheduler

Scheduler 是异步任务的权威入口，负责：

- 任务定义、状态机、重试、取消；
- 任务分解与 step 编排；
- 调度 TaskNode；
- 与 PD/MetaService 协同决定数据亲和性和执行位置；
- 提供运维/平台对接 API。

Scheduler **不负责**：

- 实际数据读写；
- 直接维护 inode 或 block data；
- 节点成员 SoT。

### 5.5 TaskNode

TaskNode 是无持久任务权威状态的任务执行者，负责：

- 执行 Scheduler 下发的 step；
- 调用 MetaService、BlockNode 完成预热、GC、降冷、重平衡等任务；
- 汇报执行结果与中间状态。

TaskNode **不负责**：

- 任务状态机；
- 任务编排；
- placement policy；
- 元数据真相维护。

## 6. Source of Truth 表

| 状态域 | 权威 SoT | 说明 |
|---|---|---|
| 全局配置 | PD | 包括集群开关、默认参数、feature flags |
| 挂载表 | PD | 将挂载表视为全局路由配置，而非 inode 元数据 |
| 节点成员信息 | PD | MetaNode / BlockNode / TaskNode 注册与存活 |
| 节点拓扑与 placement policy | PD | 包含 placement epoch |
| namespace / inode / dentry | MetaService | 路径解析、目录树、inode 关系 |
| block location / replica set | MetaService | 包括 block map version |
| file version | MetaService | 文件内容版本，供 BlockNode 缓存校验 |
| 本地缓存内容 / tier 驻留 | BlockNode | 仅对本机局部状态权威 |
| 本地缓存恢复索引 | BlockNode | 仅用于本地恢复，不是全局 SoT |
| 任务定义 / 状态 / 重试历史 | Scheduler | 任务级权威来源 |
| step 执行中的临时状态 | TaskNode | 最终仍需汇报回 Scheduler |
| UFS 持久化文件内容 | UFS | 在 write-through 基线下仍是数据持久化真源 |

## 7. 关键模型与版本语义

为避免旧架构中“ring_version 一把梭”的语义混淆，本轮设计明确区分三个版本：

### 7.1 `file_version`

- 归属：MetaService
- 含义：文件内容版本
- 用途：BlockNode 校验缓存数据是否与当前文件内容一致

### 7.2 `block_map_version`

- 归属：MetaService
- 含义：文件逻辑块到 block metadata / replica set 的映射版本
- 用途：SDK/Gateway 判断 block layout 是否发生变化

### 7.3 `placement_epoch`

- 归属：PD
- 含义：节点拓扑和 placement policy 的世代号
- 用途：SDK/Gateway 判断 placement 快照是否失效

这三个版本不能复用同一个字段，也不能由不同层重复解释。

### 7.4 `ring_version` 与未来版本语义的关系

当前仓库中的 `ring_version` 由 `HashRingManager` 提供，用于 Client 侧路由缓存失效。
在五层重构后：

- 若系统仍保留基于 ring 的单 owner placement，`ring_version` 可以作为
  `placement_epoch` 的一个具体实现或兼容字段；
- 若后续 placement 从“纯 hash ring”演进为更通用的 placement policy，
  则 `placement_epoch` 成为对外稳定语义，`ring_version` 降为兼容层内部细节。

因此，本设计**不是否定**当前 `ring_version` 的存在，而是将其纳入未来
`placement_epoch` 语义体系中统一解释。

## 8. 当前实现到目标五层的映射

为满足迁移期设计评审与 TODO DoD，本节集中给出“当前模块 -> 目标模块”的映射。

| 当前实现 | 当前职责 | 目标归属 | 迁移说明 |
|---|---|---|---|
| `src/master/worker_manager.*` | Worker 注册、心跳、状态机 | PD | 节点成员管理从 Master 迁出，成为 PD 的权威 SoT |
| `src/master/hash_ring_manager.*` | 基于 ring 的 block 路由 | PD | 先作为 placement 兼容实现保留，后续演进为更通用 placement policy |
| `src/master/mount_table.*` | 逻辑路径到 UFS 路由 | PD | 视作全局挂载配置，不再归类为 inode 元数据 |
| `src/master/inode_tree.*` | path ↔ inode、目录骨架 | MetaService | 演进为 MetaEngine + MetaService API |
| `src/master/inode_store.*` | RocksDB inode/edge 持久化 | MetaService | 演进为 `EmbeddedRocksMetaEngine` 的默认实现 |
| `src/master/path_resolver.*` | 路径解析、SyncFromUfs | MetaService | 保留解析职责，拆掉与节点管理/任务的耦合 |
| `src/master/prewarm_queue.*` | 本地异步预热队列 | Scheduler + TaskNode | 不再内嵌在 Master 中，演进为任务系统 MVP |
| `src/master/master_service_impl.*` | 混合控制面 + 元数据 RPC | PD + MetaService + 兼容层 | 按职责拆分为多个服务接口或兼容层 |
| `src/worker/page/page_store.*` | page/block 缓存引擎 | BlockNode | 继续作为核心数据面能力保留 |
| `src/worker/storage/*` | MEM/SSD/HDD tier | BlockNode | 保持归属不变，后续补齐 TTL/pin 等策略 |
| `src/worker/meta/meta_store.*` | 本地缓存恢复索引 | BlockNode | 明确只服务本地恢复，不上升为全局元数据 SoT |
| `src/worker/worker_service_impl.*` | 数据读写 + 部分后台逻辑 | BlockNode + 兼容层 | 读写 RPC 保留在 BlockNode，后台任务逻辑迁出 |
| `Heartbeat + ReconcileGc` | 后台对账/清理 | Scheduler + TaskNode | 迁移为显式任务，避免继续走隐式心跳路径 |
| `src/client/*` | 访问聚合、路由缓存、SDK | SDK / Gateway | 迁移期承担多跳编排与兼容职责 |

## 9. 最小闭环设计

### 8.1 最小读路径

在目标架构下，最小读路径应为：

1. SDK/Gateway 向 PD 请求 mount 解析与 placement 快照；
2. SDK/Gateway 向 MetaService 请求文件布局：
   - inode_id
   - file_version
   - block descriptors
   - block_map_version
3. SDK/Gateway 根据 PD 的 placement 快照将 BlockNode ID 解析为可访问 endpoint；
4. SDK/Gateway 向 BlockNode 发起读请求；
5. BlockNode 根据 `file_version` 判断缓存是否可用，miss 时回源 UFS。

### 8.2 最小写路径（保持 write-through 基线）

1. SDK/Gateway 向 PD 获取 mount 与 placement；
2. SDK/Gateway 向 MetaService 发起 `CreateFile/PrepareWrite`；
3. SDK/Gateway 向 BlockNode 写入 block/page；
4. BlockNode 先写 UFS，再更新本地缓存；
5. SDK/Gateway 向 MetaService 发起 `CompleteWrite`，更新：
   - size
   - file_version
   - block location

### 8.3 最小任务闭环

第一批任务只覆盖两类：

- `Prewarm`
- `ReconcileGc`

最小任务闭环应为：

1. 管理端或 SDK 向 Scheduler 提交任务；
2. Scheduler 向 PD 查询候选节点，向 MetaService 查询元数据范围；
3. Scheduler 将 step 下发给 TaskNode；
4. TaskNode 调用 BlockNode / MetaService 执行；
5. TaskNode 上报结果，Scheduler 完成状态收敛。

## 10. 迁移策略

### 9.1 阶段 0：边界冻结

产出：

- 五层组件职责；
- SoT 表；
- 版本语义；
- 最小闭环验收剧本；
- 兼容层策略。

在这一阶段，不做大规模代码重写。

### 10.2 阶段 1：逻辑拆分

在现有仓库中优先完成逻辑拆分。以下目录名表示**目标逻辑模块**，
并不表示当前仓库已经存在这些物理路径：

- 从 `Master` 中抽出：
  - `pd/*`
  - `meta/*`
- 从 `Worker` 中抽出：
  - `blocknode/*`
  - `background/*`
- 引入：
  - `scheduler/*`
  - `tasknode/*`
  - 兼容适配层

阶段 1 允许多个模块仍运行在原有进程中，但接口和目录边界必须先稳定。

### 10.3 阶段 2：服务拆分 MVP

形成最小独立服务：

- 独立 PD；
- 独立 MetaService（默认 EmbeddedRocksMetaEngine）；
- Worker 演进为 BlockNode；
- 独立 Scheduler；
- 独立 TaskNode；
- SDK/Gateway 负责编排多跳调用。

### 10.4 阶段 3：能力补齐

在最小闭环稳定后补齐更完整的运维和扩展能力：

- block location 的多副本、迁移与重平衡语义；
- QoS / 背压；
- 多租户 / RBAC；
- 观测与 trace；
- rewarm / reconcile / rebalance。

### 10.5 阶段 4：产品化与生态扩展

后续再进入：

- PD / Meta / Scheduler 分层 HA；
- 外部 KV 引擎适配；
- OpenDAL 落地；
- S3 Gateway；
- 多语言 SDK；
- RDMA；
- AI 训练/推理专用能力。

## 11. 第一波 issue 规划原则

本轮设计不直接展开到几十个细粒度 issue，而是先冻结 issue 分组原则：

1. **WG-00 架构冻结**：边界、SoT、版本语义、最小闭环；
2. **WG-01 逻辑拆分**：PD / Meta / BlockNode / Scheduler / TaskNode 的代码边界；
3. **WG-02 服务拆分 MVP**：最小独立服务与兼容层；
4. **WG-03 任务系统 MVP**：Prewarm / ReconcileGc；
5. **WG-04 横切能力**：QoS、观测、多租户、安全；
6. **WG-05 产品化**：HA、外部 KV、生态接入、性能专项。

后续若要生成详细 issue DAG，应从这些工作组继续细化，而不是直接把远期特性与当前主链平铺到同一层级。

## 12. 本轮设计决策

### 决策 1：挂载表归属 PD

理由：

- 挂载表更接近集群级 namespace 路由配置；
- 这样 MetaService 可以专注于 mount 内部的 inode / block metadata；
- SDK/Gateway 可以先通过 PD 做逻辑路径路由，再进入 MetaService。

### 决策 2：MetaService 必须显式持有 block location 元数据

理由：

- 当前“仅靠 hash ring 计算 owner”的模型不够支撑多副本、迁移、重平衡；
- 即使第一阶段仍保留“确定性单 owner”作为默认 placement，block location 也必须在模型层成为一等概念。

### 决策 3：Scheduler / TaskNode 第一阶段只支持两类任务

- `Prewarm`
- `ReconcileGc`

理由：

- 它们与当前仓库已有行为最接近；
- 足以验证任务系统的最小状态机与执行链路；
- 避免第一阶段就引入复杂 rebalance / cold tier compaction / AI workflow。

### 决策 4：HA 不前置到逻辑拆分之前

理由：

- 旧 `Master HA` 原型已经证明：在错误的边界上做 HA，会把错误边界固化；
- 必须先把 SoT 和职责边界稳定，再分别推进 PD / Meta / Scheduler 的 HA。

## 13. 风险与未决问题

### 12.1 高风险

1. **PD 具体实现技术路线**  
   语义上对齐 TiKV PD，但是否 embed etcd、如何在当前 C++ 仓库中落地，仍需单独方案。

2. **OpenDAL 在 C++ 运行时中的接入方式**  
   第一阶段只冻结 capability 模型，不直接绑定最终实现。

3. **外部 KV 引擎的事务语义**  
   若后续接 KinsDB / TiKV / FDB，MetaEngine 接口粒度和事务边界需要单独设计。

### 12.2 中风险

1. `block_map_version` 的更新粒度：按文件、按 extent、还是按 block set；
2. SDK/Gateway 在迁移期会暂时变复杂，需要明确兼容层策略；
3. 旧 heartbeat GC 如何平滑迁移到 Scheduler/TaskNode 流水线。

## 14. 验收标准

本轮设计阶段完成的标志不是“代码全实现”，而是以下文档与规划达成一致：

- 五层架构职责图稳定；
- SoT 表稳定；
- 最小读/写/任务闭环稳定；
- 迁移阶段稳定；
- 后续 issue 生成原则稳定。

