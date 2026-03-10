# Todo: P1-08A MountTable 核心映射与 RPC

> 关联 Plan: [plan_p1-08a-mount-table.md](./plan_p1-08a-mount-table.md)

## Todo 列表

| ID | 内容 | 状态 | DoD |
|----|------|------|-----|
| T1 | 实现 src/master/mount_table.h/.cpp | completed | Mount/Unmount/Resolve/ListMounts，最长前缀匹配，重复挂载/未命中错误 |
| T2 | 在 MasterServiceImpl 中接线 Mount/Unmount/ListMounts RPC | completed | 调用 mount_table_，Status 转 gRPC Status |
| T3 | 实现 tests/master/mount_table_test.cpp | completed | 覆盖 Resolve 最长前缀、重复挂载、未命中、ListMounts |
| T4 | 更新 CMakeLists | completed | master 库含 mount_table，tests 含 mount_table_test |
| T5 | 构建与测试验证 | completed | cd build && cmake .. && cmake --build . && ctest -R mount 通过 |

## 执行约束

- 全程最多一个 `in_progress`。
- 完成一项后更新状态为 `completed`，再启动下一项。
