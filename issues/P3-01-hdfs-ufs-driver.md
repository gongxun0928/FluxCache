# P3-01: HDFS UFS 驱动

## 阶段与优先级

Phase H — 后端扩展 | P1

## 依赖

- [P1-11](./P1-11-ufs-localfs.md)

## 描述

实现 HDFS 兼容的 UFS 驱动，作为后端扩展能力，不提前进入 MVP 主链。

## 交付物

- `src/ufs/hdfs_ufs.h/.cpp`
- 在 `UfsFactory` 中注册 `hdfs`
- 基础鉴权与错误映射

## 验收标准

- [ ] 可从 HDFS 读取文件到 FluxCache。
- [ ] 可通过 FluxCache 写入文件到 HDFS。
- [ ] NameNode 不可达时返回明确 `Unavailable`。
- [ ] 文件不存在时返回 `NotFound`。
- [ ] 单元测试通过；集成测试可基于 MiniDFS 或等价环境。

## 涉及目录

```text
src/ufs/
tests/ufs/
```
# P3-01: HDFS UFS 驱动

## 阶段与优先级

Phase H — 后端扩展 | P1

## 依赖

- [P1-11](./P1-11-ufs-localfs.md)

## 描述

实现 HDFS 兼容的 UFS 驱动，作为后端扩展能力，不提前进入 MVP 主链。

## 交付物

- `src/ufs/hdfs_ufs.h/.cpp`
- 在 `UfsFactory` 中注册 `hdfs`
- 基础鉴权与错误映射

## 验收标准

- [ ] 可从 HDFS 读取文件到 FluxCache。
- [ ] 可通过 FluxCache 写入文件到 HDFS。
- [ ] NameNode 不可达时返回明确 `Unavailable`。
- [ ] 文件不存在时返回 `NotFound`。
- [ ] 单元测试通过；集成测试可基于 MiniDFS 或等价环境。

## 涉及目录

```text
src/ufs/
tests/ufs/
```
