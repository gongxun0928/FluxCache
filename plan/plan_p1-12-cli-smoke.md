# Plan: P1-12 CLI smoke 工具

> Status: Done

## Goal

- **Problem**: 开发与联调缺少面向最小集群的 smoke CLI，无法快速验证 write -> read -> stat 流程。
- **Target outcome**: 提供 `fluxcache-cli` 可执行文件，支持 mount/unmount/ls-mounts/read/write/stat 子命令；在最小集群上可完成一次 write -> read -> stat 验证；集群不可达时输出明确错误；只覆盖已存在服务端能力，不额外发明新 RPC。

## Steps

1. 在 MasterClient 中补充 Unmount、ListMounts 封装（proto 已有，仅补齐 client 调用）。
2. 创建 `src/client/cli/main.cpp`，实现子命令解析与 FluxCacheClient 调用。
3. 在 `src/client/CMakeLists.txt` 中新增 `fluxcache-cli` 可执行目标。
4. 添加 CLI smoke 集成测试（或复用现有 E2E 框架做 write -> read -> stat 验证）。
5. 执行 `cd build && cmake .. && cmake --build . && ctest --output-on-failure` 验证。

## 测试设计（Design Gate 产出）

- **主路径**：集成测试启动 Master+Worker，挂载 FakeUfs，通过 CLI 执行 write -> read -> stat，校验内容与 stat 输出。
- **失败路径**：集群不可达时（错误 master 地址），CLI 输出明确错误信息（如 "cluster unreachable" 或 Unavailable 消息）。
- **边界**：空 mount 列表时 ls-mounts 输出空；stat 不存在的 path 返回 NotFound。

## Risks & Assumptions

- **Risk**: MasterClient 当前无 Unmount/ListMounts，需新增。**Mitigation**: 按 Mount 模式实现，proto 已定义。
- **Assumption**: CLI 通过 `--config <path>` 加载 YAML 配置，与 master/worker 进程一致；不引入新配置格式。

## To Confirm

- [x] 只覆盖已存在 RPC：Mount、Unmount、ListMounts、GetFileInfo、CreateFile、CompleteFile、ReadPages、WritePages、GetHashRing。
- [x] 验收标准以 issues/P1-12-cli-minimal.md 为准。

## 变更分级

P2 — 局部新增，不影响已有核心路径。
