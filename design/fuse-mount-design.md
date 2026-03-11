# FUSE 挂载基础能力设计

> 日期: 2026-03-11
> 状态: P2-06 设计
> 关联: [P2-06](../issues/P2-06-fuse-mount.md)

## 1. 概述

基于 FluxCache SDK MVP 提供 FUSE 挂载主路径，使标准文件工具（cat、echo 等）可访问 FluxCache。本设计仅覆盖主路径系统调用，不承诺完整 POSIX 语义。

## 2. 设计决策

### 2.1 依赖

- **libfuse3**：通过 `find_package(PkgConfig)` + `pkg_check_modules(FUSE3 fuse3)` 查找
- 未找到时：跳过 FUSE 目标构建，不阻断主构建
- 需定义 `FUSE_USE_VERSION=31` 后再包含 fuse 头文件

### 2.2 路径映射

- FUSE 挂载点根 "/" 对应 FluxCache 逻辑根
- 命令行参数 `--root <path>` 指定 FluxCache 逻辑根，默认 `/mnt`
- 映射规则：`sdk_path = root + fuse_path`，例如 FUSE "/foo" → SDK "/mnt/foo"

### 2.3 操作映射

| FUSE 操作 | SDK 调用 | 错误处理 |
|-----------|----------|----------|
| getattr   | Stat(path) | NotFound → -ENOENT，InvalidArgument → -EINVAL |
| open      | Open(path, mode) | NotFound → -ENOENT |
| release   | handle->Close() | 透传 |
| read      | handle->Read(buf, offset, size) | 透传 |
| write     | handle->Write(offset, data) | 透传 |
| create    | Create(path) | AlreadyExists → -EEXIST |

### 2.4 不支持操作

以下操作**必须返回明确错误**，禁止 silent fallback：

- rename → -ENOTSUP
- link → -ENOTSUP
- symlink → -ENOTSUP
- readlink → -ENOTSUP
- mkdir → -ENOTSUP
- rmdir → -ENOTSUP
- readdir → -ENOTSUP（MVP 无 List）
- chmod/chown/utimens → -ENOTSUP
- xattr → -ENOTSUP
- mmap → -ENOTSUP

### 2.5 打开文件句柄管理

- 使用 `fuse_file_info::fh` 存储 `FileHandle*`（或包装结构）
- open 时创建 FileHandle 并存入 fh
- release 时 Close 并释放

### 2.6 配置来源

- 命令行：`--master <host>`, `--port <port>`, `--root <path>`
- 或通过 `--config <yaml>` 加载 SDKConfig（复用 ClientConfig 结构）

## 3. 模块边界

```
fuse_main.cpp     → 解析参数、创建 SDK、注册 fuse_ops、fuse_main()
fuse_ops.h/.cpp   → 实现 getattr/open/release/read/write/create 及 ENOTSUP 桩
```

## 4. 测试设计

| 场景 | 输入 | 预期 |
|------|------|------|
| cat 读文件 | 挂载后 cat /path/to/file | 输出与 SDK Read 一致 |
| echo 写文件 | echo "data" > /path/to/file | 写路径走通，cat 可读回 |
| 不支持操作 | rename、mkdir 等 | 返回 -ENOTSUP，不崩溃 |
| 无 libfuse | 系统无 fuse3 | CMake 跳过 FUSE 目标，主构建成功 |

**验收策略**：集成测试需启动 Master+Worker+FakeUfs，挂载 FUSE，执行 cat/echo 验证。若无 FUSE 环境，提供手动验证文档。

## 5. 非目标

- rename、link、xattr、mmap
- 完整目录语义（readdir、mkdir、rmdir）
- 多线程 FUSE（单线程即可满足 MVP）
