# Todo: P1-12 CLI smoke 工具

> 关联 Plan: plan/plan_p1-12-cli-smoke.md

## Todo 列表

| ID | 任务 | 状态 | DoD |
|---|---|---|---|
| T1 | 在 MasterClient 中补充 Unmount、ListMounts | completed | master_client.h/.cpp 声明并实现，与 Mount 行为一致 |
| T2 | 创建 src/client/cli/main.cpp | completed | 子命令 mount/unmount/ls-mounts/read/write/stat 可解析并调用 FluxCacheClient |
| T3 | 添加 fluxcache-cli 可执行目标到 CMakeLists | completed | 构建成功，fluxcache-cli 可执行 |
| T4 | 添加 CLI smoke 集成测试 | completed | 测试 write -> read -> stat 验证通过 |
| T5 | 执行 build + ctest 验证 | completed | 全部通过 |

## 执行顺序

T1 -> T2 -> T3 -> T4 -> T5

## 当前 in_progress

无
