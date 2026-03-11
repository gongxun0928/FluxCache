# Todo: P1-15C Client.Write 最小实现

> Status: Done
> Plan: [plan_p1-15c-client-write.md](./plan_p1-15c-client-write.md)

## Todo 列表

| # | 任务 | 状态 | DoD |
|---|------|------|-----|
| 1 | 扩展 WritePagesResponse 增加 ufs_mtime_ms | completed | Worker 写入后设置 mtime，proto 编译通过 |
| 2 | MasterClient 增加 CreateFile、CompleteFile | completed | 可调用 Master RPC |
| 3 | FluxCacheClient::Write 主逻辑 | completed | 实现 CreateFile/GetFileInfo → 切分 → WritePages → CompleteFile |
| 4 | tests/client/write_test.cpp | completed | 单页、跨页、跨 block、失败不 CompleteFile 验收通过 |
| 5 | Testing Gate | completed | 构建、ctest -R write、lint 通过 |
