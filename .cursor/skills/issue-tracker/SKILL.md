# Issue Tracker — 批量 Issue 处理与状态编排

## 触发条件

- 用户要求处理某一批 issue；
- 用户要求继续当前批次；
- 用户要求查询 issue 状态或进度；
- 用户要求处理单个 issue 并检查依赖。

## 输入

- `plan/issue-status.md`：issue 状态真相源；
- `plan/active-batch.md`：当前活跃批次（如存在）；
- `issues/index.md`：依赖关系与执行顺序参考；
- `issues/<issue-id>.md`：单个 issue 的详细需求与验收标准。

## 执行步骤

1. 读取 `plan/issue-status.md`，必要时读取 `plan/active-batch.md` 和 `issues/index.md`。
2. 解析用户意图，判断是新建批次、继续批次、处理单个 issue，还是只做状态汇报。
3. 若新建批次：
   - 按用户条件筛选 issue；
   - 递归展开未解除的依赖；
   - 按依赖关系拓扑排序；
   - 创建或更新 `plan/active-batch.md`。
4. 若处理单个 issue：
   - 先检查依赖是否全部为 `completed` 或 `pr_merged`；
   - 有阻塞则汇报并停止；
   - 无阻塞则将状态更新为 `in_progress`。
5. 实现当前 issue：
   - 读取 `issues/<issue-id>.md`；
   - 按 `stable-agent-delivery` 或标准开发流程完成设计、编码、测试、评审；
   - 根据用户要求决定是否提交 commit 或创建 PR。
6. 完成一个 issue 后更新 `plan/issue-status.md` 与 `plan/active-batch.md`，再重新读取状态源，决定下一个可处理 issue。
7. 若所有待处理项都被阻塞，则输出阻塞摘要并等待用户决策；若批次完成，则输出批次结果摘要。

## 输出

- 更新后的 `plan/issue-status.md`；
- 更新后的 `plan/active-batch.md`（如适用）；
- 当前 issue 或批次的处理结果；
- 阻塞项、下一步可处理项或最终完成摘要。
