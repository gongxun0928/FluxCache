# P2-06: FUSE 挂载基础能力

## 阶段与优先级

Phase G — 访问入口 | P1

## 依赖

- [P2-09](./P2-09-sdk-basic.md)

## 描述

基于 SDK MVP 提供 FUSE 挂载主路径，使标准文件工具可以访问 FluxCache。

本 issue 只覆盖主路径系统调用，不承诺完整 POSIX 语义。

## 交付物

- `src/client/fuse/fuse_main.cpp`
- `src/client/fuse/fuse_ops.h/.cpp`
- `getattr`
- `open` / `release`
- `read`
- `write`
- `create`

非目标：

- `rename`
- `link`
- `xattr`
- `mmap`
- 完整目录语义

## 验收标准

- [ ] `cat` 可读取通过 SDK MVP 已支持的文件。
- [ ] `echo "data" > file` 可走通写路径主链。
- [ ] 不支持的操作返回明确错误，而不是 silent fallback。

## 涉及目录

```text
src/client/fuse/
```
