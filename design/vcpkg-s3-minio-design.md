# vcpkg 依赖管理与 S3/MinIO UFS 真实实现设计

> 日期: 2026-03-11
> 状态: 设计

## 1. 目标

- 通过 **vcpkg** 统一管理 FluxCache 依赖
- 引入 **minio-cpp** 作为 S3/MinIO 兼容对象存储的 C++ SDK
- 将现有 S3UFS stub 替换为基于 minio-cpp 的真实读写实现

## 2. 技术选型

| 组件 | 选型 | 说明 |
|------|------|------|
| 包管理 | vcpkg | 微软 C++ 包管理器，支持 manifest 模式 |
| S3 SDK | minio-cpp | MinIO 官方 C++ 客户端，兼容 S3 API，vcpkg 有 port |
| 兼容性 | S3 / MinIO / 阿里云 OSS 等 | 所有 S3 兼容对象存储 |

minio-cpp 在 vcpkg 中为 `minio-cpp`，版本 v0.3.0，依赖：curlpp, inih[cpp], nlohmann-json, openssl, pugixml, zlib。

## 3. S3 UFS 语义映射

UFS 接口与 S3 对象存储的映射：

| UFS 操作 | S3/MinIO 对应 | 说明 |
|----------|---------------|------|
| Read(path, offset, size) | GetObject + Range | path → object key，支持 range 读取 |
| Write(path, offset, data) | GetObject + 修改 + PutObject | 小文件可整对象覆盖；大文件需 multipart 或读-改-写 |
| GetStatus(path) | HeadObject / StatObject | 获取 size、LastModified |
| List(path) | ListObjectsV2 | path 作为 prefix，返回对象列表 |
| Delete(path) | DeleteObject | 删除对象 |
| Rename(src, dst) | CopyObject + DeleteObject | 非原子 |
| Mkdirs(path) | 无操作或 PutObject 空对象 | S3 无目录，用 0 字节对象或仅作 prefix |

**路径约定**：UFS path 如 `/a/b/file` 映射为 S3 object key `a/b/file`（去掉前导 `/`）。

## 4. Authority 与配置

当前 `CreateUFS(scheme, authority, out)` 中，S3 的 authority 格式：

```
s3://[endpoint/]bucket
```

- **仅 bucket**：`s3://my-bucket` → authority=`my-bucket`，使用默认 endpoint（环境变量或 AWS 默认）
- **自定义 endpoint**：`s3://minio:9000/my-bucket` → authority=`minio:9000/my-bucket`

**凭证来源**（优先级从高到低）：
1. 环境变量：`AWS_ACCESS_KEY_ID`、`AWS_SECRET_ACCESS_KEY`（MinIO 可用 `MINIO_ROOT_USER`、`MINIO_ROOT_PASSWORD`）
2. 后续扩展：配置文件中的 mount 级 S3 配置

## 5. 错误映射

| S3/MinIO 错误 | FluxCache Status |
|--------------|------------------|
| NoSuchKey / 404 | NotFound |
| AccessDenied / 403 | 可考虑 ResourceExhausted 或新增 PermissionDenied |
| 连接失败 / 超时 | Unavailable |
| 无效参数 | InvalidArgument |
| 其他 | IOError |

## 6. 构建策略

- **可选编译**：`FLUXCACHE_ENABLE_S3`（已有）为 ON 时，启用 minio-cpp 与 S3UFS 真实实现
- **vcpkg manifest**：在项目根目录添加 `vcpkg.json`，声明对 minio-cpp 的依赖（仅在 S3 启用时生效）
- **fallback**：`FLUXCACHE_ENABLE_S3=OFF` 时，继续使用现有 stub，返回 Unavailable

## 7. 涉及目录

```
/
├── vcpkg.json              # 新增：manifest 依赖
├── CMakeLists.txt          # 修改：vcpkg 工具链、条件编译
├── src/ufs/
│   ├── s3_ufs.h/.cpp       # 修改：真实实现
│   └── s3_config.h         # 新增（可选）：解析 authority、环境变量
└── tests/
    └── ufs/
        └── s3_ufs_test.cpp # 新增：单元测试（可 mock 或 MinIO）
```

## 8. 风险与假设

- **minio-cpp API**：需确认 GetObject range、PutObject、ListObjects 等接口与 UFS 语义的对应关系
- **大文件 Write**：offset > 0 的写需要读-改-写或 multipart，可能影响性能；Phase 1 可先支持小文件/整对象覆盖
- **vcpkg 工具链**：用户需安装 vcpkg 并配置 `CMAKE_TOOLCHAIN_FILE`
