# Todo: P1-03 Phase 1 最小 RPC 契约

> 关联 Plan: plan/plan_p1-03-grpc-proto.md

## Todo 列表

| # | 任务 | 状态 | DoD |
|---|------|------|-----|
| 1 | 引入 protobuf/gRPC 依赖 | completed | 顶层 CMakeLists 中 find_package 或 FetchContent 可用 |
| 2 | 实现 common.proto | completed | WorkerEndpoint、FileInfo 等消息定义完整 |
| 3 | 实现 master.proto | completed | Master 服务与 GetFileInfoResponse 等消息定义完整 |
| 4 | 实现 worker.proto | completed | Worker 服务与 ReadPages/WritePages/Heartbeat 定义完整 |
| 5 | 更新 proto CMakeLists 生成 C++ | completed | fluxcache_proto 含 pb + grpc 生成代码，可被 master/worker/client 链接 |
| 6 | 实现 tests/proto/proto_test.cpp | completed | 编解码与字段可达性测试通过 |
| 7 | 注册 proto 测试 | completed | ctest -R proto 可执行 |
| 8 | 构建与测试验证 | completed | cmake --build . && ctest -R proto 通过 |

## 约束

- 全程最多一个 `in_progress`
- DoD 可验证
