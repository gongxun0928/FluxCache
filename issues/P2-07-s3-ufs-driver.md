# P2-07: S3 UFS 驱动

## 阶段与优先级

Phase H — 后端扩展 | P1

## 依赖

- [P1-11](./P1-11-ufs-localfs.md)

## 描述

实现 S3 兼容对象存储的 UFS 驱动，扩展后端类型，但不改变核心缓存和协议语义。

## 交付物

- `src/ufs/s3_ufs.h/.cpp`
- 在 `UfsFactory` 中注册 `s3`
- 最小鉴权与错误映射

## 验收标准

- [ ] 可从 S3 读取对象到 FluxCache。
- [ ] 可通过 FluxCache 写入对象到 S3。
- [ ] 鉴权失败返回明确权限错误。
- [ ] 对象不存在返回明确 `NotFound`。
- [ ] 单元测试通过；集成测试可基于 MinIO 或等价环境。

## 涉及目录

```text
src/ufs/
tests/ufs/
```
# P2-07: S3 UFS 驱动

## 阶段与优先级

Phase H — 后端扩展 | P1

## 依赖

- [P1-11](./P1-11-ufs-localfs.md)

## 描述

实现 S3 兼容对象存储的 UFS 驱动，扩展后端类型，但不改变核心缓存和协议语义。

## 交付物

- `src/ufs/s3_ufs.h/.cpp`
- 在 `UfsFactory` 中注册 `s3`
- 最小鉴权与错误映射

## 验收标准

- [ ] 可从 S3 读取对象到 FluxCache。
- [ ] 可通过 FluxCache 写入对象到 S3。
- [ ] 鉴权失败返回明确权限错误。
- [ ] 对象不存在返回明确 `NotFound`。
- [ ] 单元测试通过；集成测试可基于 MinIO 或等价环境。

## 涉及目录

```text
src/ufs/
tests/ufs/
```
