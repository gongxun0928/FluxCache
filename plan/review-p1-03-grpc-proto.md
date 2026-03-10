# Review: P1-03 Phase 1 最小 RPC 契约

> 关联 Plan: plan/plan_p1-03-grpc-proto.md

## 评审结论

- **结论**：通过
- **风险等级**：低

## 发现列表

无阻断性问题。

## 改动说明

- 新增 `src/proto/common.proto`：WorkerEndpoint、FileInfo
- 新增 `src/proto/master.proto`：MasterService（GetFileInfo、CreateFile、CompleteFile、DeleteFile、Mount/Unmount/ListMounts、RegisterWorker、GetHashRing）及对应请求/响应消息
- 新增 `src/proto/worker.proto`：WorkerService（ReadPages、WritePages、Heartbeat）及对应请求/响应消息
- 更新 `src/proto/CMakeLists.txt`：使用 protobuf/gRPC 生成 C++ 代码，构建 fluxcache_proto 静态库
- 更新 `CMakeLists.txt`：引入 find_package(Protobuf)、find_package(gRPC)
- 新增 `tests/proto/proto_test.cpp`：契约测试（编解码、字段可达性）
- 更新 `tests/CMakeLists.txt`：注册 proto 测试

## 契约要求符合性

| 要求 | 状态 |
|------|------|
| FileInfo: inode_id, size, block_size, ufs_mtime_ms, is_directory | ✓ |
| GetFileInfoResponse: file_info, ring_version, workers, ufs_uri, ufs_path | ✓ |
| ReadPagesRequest: block_id, page_indices, expected_mtime_ms, ufs_uri, ufs_path | ✓ |
| WritePagesRequest: block_id, page_indices, data, ufs_uri, ufs_path | ✓ |
| Heartbeat 预留 GC 字段 | ✓ |

## 测试执行记录

| 命令 | 结果 |
|------|------|
| `cd build && cmake ..` | 成功 |
| `cmake --build .` | 成功 |
| `ctest -R proto --output-on-failure` | 6/6 通过 |
| `ctest --output-on-failure` | 5/5 全量通过 |

## 残余风险与测试缺口

- 无。Phase 1 读写主路径所需字段全部齐备，未提前承诺 Rename、目录 CRUD、批量读、流式 pipeline、HA 日志复制字段。
