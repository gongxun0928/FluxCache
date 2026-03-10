# Plan: P1-03 Phase 1 最小 RPC 契约

> Status: Done

## Goal

- **Problem**: Phase 1 MVP 需要稳定的 RPC 契约，供 Master、Worker、Client 通信。
- **Target outcome**: 定义并实现 Phase 1 最小 proto 集合，生成 C++ 代码，提供 `fluxcache_proto` 静态库，通过契约测试验证编解码与字段可达性。

## Steps

1. 在顶层 CMakeLists 中引入 protobuf 与 gRPC（`find_package` 优先，可选 `FetchContent` 回退）。
2. 创建 `src/proto/common.proto`：WorkerEndpoint、FileInfo 等公共消息。
3. 创建 `src/proto/master.proto`：Master 服务（GetFileInfo、CreateFile、CompleteFile、DeleteFile、Mount/Unmount/ListMounts、RegisterWorker、GetHashRing）。
4. 创建 `src/proto/worker.proto`：Worker 服务（ReadPages、WritePages、Heartbeat）。
5. 更新 `src/proto/CMakeLists.txt`：使用 protobuf/gRPC 生成 C++ 代码，构建 `fluxcache_proto` 静态库。
6. 创建 `tests/proto/proto_test.cpp`：契约测试（编解码、字段可达性）。
7. 在 `tests/CMakeLists.txt` 中注册 proto 测试。

## Risks & Assumptions

- **Risk**: 系统未安装 protobuf/gRPC 导致构建失败。**Mitigation**: 优先 `find_package`，文档说明需 `brew install grpc`（macOS）或 `apt install libgrpc++-dev`（Linux）。
- **Assumption**: Phase 1 不承诺 Rename、目录 CRUD、批量读、流式 pipeline、HA 日志复制字段；Heartbeat 预留 GC 字段但不实现完整逻辑。

## To Confirm

- [x] 契约要求以 issues/P1-03-grpc-proto.md 为准。
- [x] 设计参考 design/block-id-and-file-layout.md、design/metadata-design.md。

## 变更分级

P1 — 涉及 RPC 契约与公共接口，影响 Master、Worker、Client 模块。

## 测试设计

- **编解码**：对 FileInfo、GetFileInfoResponse、ReadPagesRequest、WritePagesRequest 等消息执行 set → SerializeToString → ParseFromString → get，验证往返一致性。
- **字段可达性**：验证契约要求中列出的所有字段均可正确设置与读取。
