# P2-07A: vcpkg 依赖管理与 minio-cpp 集成

## 阶段与优先级

Phase H — 后端扩展 | P1

## 依赖

- [P1-01](./P1-01-project-scaffolding.md)
- [P2-07](./P2-07-s3-ufs-driver.md)（当前为 stub，本 issue 为其前置）

## 描述

将 FluxCache 构建系统接入 vcpkg，并引入 minio-cpp 作为可选依赖。为 P2-07B（S3UFS 真实实现）提供 SDK 与构建基础。

## 交付物

- `vcpkg.json`：manifest 模式依赖声明（minio-cpp 等）
- `CMakeLists.txt` 修改：支持 vcpkg 工具链、`FLUXCACHE_ENABLE_S3` 时 find_package(minio-cpp)
- `README.md` / `README.zh-CN.md`：补充 vcpkg 构建说明

## 验收标准

- [ ] 使用 `-DCMAKE_TOOLCHAIN_FILE=[vcpkg]/scripts/buildsystems/vcpkg.cmake` 可成功配置并构建
- [ ] `FLUXCACHE_ENABLE_S3=ON` 时，minio-cpp 被正确找到并链接
- [ ] `FLUXCACHE_ENABLE_S3=OFF` 时，构建不受影响，不依赖 minio-cpp
- [ ] 现有测试（不含 S3）全部通过

## 涉及目录

```text
/
CMakeLists.txt
README.md
README.zh-CN.md
```
