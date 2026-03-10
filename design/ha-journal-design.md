# HA Journal 设计

> 日期: 2026-03-10
> 状态: Spike / 设计收敛中

## 1. 目标

在不打断当前 Phase 1 主线的前提下，提前收敛 Journal 与现有 RocksDB 元数据持久化之间的职责边界，为后续 HA / Raft 实现提供稳定设计基线。

## 2. 当前结论

当前系统中：

- `InodeTree / InodeStore (RocksDB)` 是 Master 重启恢复的 source of truth。
- Journal 还不是当前实现中的恢复主链路。
- 因此，HA/Journaling 在现阶段只能以设计 spike 形式推进，不能直接改写主路径语义。

## 3. 必须回答的问题

### 3.1 Journal 的职责

Journal 在后续设计中可以承担以下一种或组合职责：

- 复制通道：将元数据变更复制到其他 Master
- 审计通道：保留可追踪的变更记录
- 恢复主链路：重启时通过 replay 恢复状态

当前不允许在未定职责的情况下同时承担全部角色。

### 3.2 与 RocksDB 的关系

推荐边界：

- `Phase 1 / 当前实现`：RocksDB 负责本地恢复，Journal 不介入恢复
- `HA 演进阶段`：Journal 作为复制日志，RocksDB 作为状态机快照或应用后结果

换句话说，未来实现更接近：

```text
Client RPC
  -> Journal append / replicate
  -> apply to state machine
  -> persist state snapshot / RocksDB
```

而不是：

```text
同时写 Journal
同时写 RocksDB
重启时再决定谁说了算
```

## 4. 最小 JournalEntry 范围

应只覆盖真正改变 Master 元数据状态的操作：

- `MOUNT`
- `UNMOUNT`
- `FILE_CREATE`
- `FILE_COMPLETE`
- `FILE_DELETE`
- `DIRECTORY_CREATE`
- `DIRECTORY_DELETE`
- `WORKER_REGISTER`（可选，若视为可恢复元数据）

## 5. 当前建议

- 先完成设计与最小接口草图。
- 不在当前主线中承诺 replay + checkpoint + 完整恢复。
- 不在当前主线中承诺选主和故障切换。

## 6. 后续实现拆分建议

1. `JournalEntry` 模型与序列化
2. Journal writer / reader 原型
3. 元数据写路径的 journal hook
4. checkpoint 与快照恢复
5. Raft 复制与 leader 选举
