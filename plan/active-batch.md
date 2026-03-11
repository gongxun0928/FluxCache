# 当前活跃批次

> 由 issue-tracker 维护

## 批次目标

vcpkg + minio-cpp 实现 S3/MinIO 真实读写

## 执行顺序

1. **P2-07A**：vcpkg 依赖管理与 minio-cpp 集成
2. **P2-07B**：S3UFS 真实实现（依赖 P2-07A）

## 当前状态

| Issue | 状态 | 备注 |
|-------|------|------|
| P2-07A | completed | 2026-03-11 |
| P2-07B | completed | 2026-03-11 |

## 依赖解除

- P2-07A 依赖：P1-01 ✓、P2-07 ✓
- P2-07B 依赖：P2-07A（待完成）、P1-11 ✓
