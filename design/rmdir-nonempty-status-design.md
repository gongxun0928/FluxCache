# Rmdir Non-Empty Status Design

## 1. 背景

`MasterServiceImpl::Rmdir` 在目录非空时返回 gRPC `FAILED_PRECONDITION`。当前 `MasterClient::Rmdir` 未识别该状态码，会退化为 `Status::IOError()`，进一步在 FUSE 层被映射为 `-EIO`，导致 `rmdir`/`rm -r` 看见错误的 errno。

## 2. 目标

- 保持服务端 `Rmdir` 的 gRPC 语义不变；
- 让客户端能把“目录非空”与一般 I/O 错误区分开；
- 让 FUSE `rmdir` 返回 `-ENOTEMPTY`，与 POSIX 语义一致。

## 3. 非目标

- 不改造其他 RPC 的错误语义；
- 不改造服务端 `DeleteInode` 的底层返回结构；
- 不在本次修复中统一引入泛化的 gRPC ↔ Status 全量映射层。

## 4. 方案对比

### 方案 A：把 `FAILED_PRECONDITION` 映射到现有 `kInvalidArgument`

- 优点：改动小；
- 缺点：语义错误，FUSE 会得到 `EINVAL` 而不是 `ENOTEMPTY`。

### 方案 B：新增泛化 `kFailedPrecondition`

- 优点：和 gRPC 语义对齐；
- 缺点：FUSE 若统一把该状态映射成 `ENOTEMPTY`，会误伤其他未来的前置条件失败场景。

### 方案 C：新增精确状态 `kDirectoryNotEmpty`（推荐）

- 优点：仅覆盖当前 bug 所需语义，FUSE 可以准确映射到 `ENOTEMPTY`；
- 缺点：客户端状态枚举新增一个面向场景的错误码。

## 5. 推荐方案

采用 **方案 C**：

1. 在 `StatusCode`/`Status` 中新增 `kDirectoryNotEmpty` / `Status::DirectoryNotEmpty()`；
2. `MasterClient::Rmdir` 遇到 gRPC `FAILED_PRECONDITION` 时返回该状态；
3. FUSE `ToErrno` 将 `kDirectoryNotEmpty` 映射到 `-ENOTEMPTY`；
4. 补充回归测试，覆盖 SDK 端的非空目录删除错误语义与基础状态构造。

## 6. 风险与假设

- 风险：`FAILED_PRECONDITION` 在 `Rmdir` 服务端消息里也可能表示其他删除失败原因；当前实现与用户 issue 一致，优先保证“目录非空”这一可观测语义；
- 假设：现阶段 FUSE 中使用该状态码的入口仅需修复 `rmdir` 相关行为，不要求一次性泛化全部前置条件错误。

## 7. 验收/测试设计

- RED：新增 SDK 回归测试，构造非空目录后调用 `Rmdir`，期望返回 `StatusCode::kDirectoryNotEmpty`；
- GREEN：实现客户端状态映射与 FUSE errno 映射，使测试通过；
- 验证：
  - `cd build && cmake --build .`
  - `cd build && ctest --output-on-failure`
  - `clang-format -style=Google -n --Werror <changed-files>`
