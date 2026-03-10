# P1-15C: Client.Write 最小实现

## 阶段与优先级

Phase C — 数据面闭环 | P1

## 依赖

- [P1-07](./P1-07-client-skeleton.md)
- [P1-15A](./P1-15a-master-create-and-complete-file.md)
- [P1-15B](./P1-15b-worker-write-pages.md)
- [P1-16](./P1-16-core-types.md)

## 描述

实现 Client 侧写路径：按 block/page 切片，调用 `WritePages`，成功后调用 `CompleteFile` 更新元数据。

## 交付物

- `FluxCacheClient::Write`
- 对新文件走 `CreateFile`
- 对已存在文件走 `GetFileInfo`
- 写后触发 `CompleteFile`

## 验收标准

- [ ] 单页写入后读取内容正确。
- [ ] 跨页写入后读取内容正确。
- [ ] 跨 block 写入后读取内容正确。
- [ ] 写入失败时不会调用 `CompleteFile` 提交错误元数据。

## 涉及目录

```text
src/client/
tests/client/
```
