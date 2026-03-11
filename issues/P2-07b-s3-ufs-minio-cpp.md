# P2-07B: S3UFS 真实实现（基于 minio-cpp）

## 阶段与优先级

Phase H — 后端扩展 | P1

## 依赖

- [P2-07A](./P2-07a-vcpkg-minio-cpp.md)
- [P1-11](./P1-11-ufs-localfs.md)

## 描述

在 P2-07A 完成 vcpkg 与 minio-cpp 集成的基础上，将 S3UFS 从 stub 替换为基于 minio-cpp 的真实读写实现，支持 S3 兼容对象存储（含 MinIO）。

## 交付物

- `src/ufs/s3_ufs.h/.cpp`：真实实现（Read、Write、GetStatus、List、Delete、Rename、Mkdirs）
- 在 `UfsFactory` 中保持 `s3` 注册（已有）
- 最小鉴权（环境变量）与错误映射

## 验收标准

- [ ] 可从 S3/MinIO 读取对象到 FluxCache
- [ ] 可通过 FluxCache 写入对象到 S3/MinIO
- [ ] 鉴权失败返回明确错误
- [ ] 对象不存在返回 `NotFound`
- [ ] 单元测试通过；集成测试可基于 MinIO 或等价环境

## 涉及目录

```text
src/ufs/
tests/ufs/
```

## Authority 格式

- `s3://bucket` → authority=`bucket`，默认 endpoint
- `s3://host:port/bucket` → authority=`host:port/bucket`，自定义 endpoint（如 MinIO）

凭证：`AWS_ACCESS_KEY_ID`、`AWS_SECRET_ACCESS_KEY` 或 `MINIO_ROOT_USER`、`MINIO_ROOT_PASSWORD`
