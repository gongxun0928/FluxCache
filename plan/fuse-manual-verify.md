# P2-06 FUSE 手动验证步骤

> 当系统有 libfuse3 且 `FLUXCACHE_ENABLE_FUSE=ON` 时，可按以下步骤验证。

## 前置条件

1. 构建时启用 FUSE：`cmake -DFLUXCACHE_ENABLE_FUSE=ON ..`
2. 系统已安装 libfuse3：`brew install macfuse`（macOS）或 `apt install libfuse3-dev`（Linux）
3. Master、Worker 已启动，且已 Mount 并 RegisterWorker

## 验证步骤

### 1. 挂载

```bash
mkdir -p /tmp/fc_mount
./fluxcache-fuse -o master=127.0.0.1 -o port=9090 -o root=/mnt /tmp/fc_mount
# 或后台运行: ./fluxcache-fuse -f -o master=127.0.0.1 -o port=9090 /tmp/fc_mount &
```

### 2. cat 读文件

先用 SDK 或 CLI 创建并写入文件，例如 `/mnt/test.txt`，然后：

```bash
cat /tmp/fc_mount/test.txt
# 应输出文件内容
```

### 3. echo 写文件

```bash
echo "data" > /tmp/fc_mount/newfile.txt
cat /tmp/fc_mount/newfile.txt
# 应输出 "data"
```

### 4. 不支持操作返回明确错误

```bash
mkdir /tmp/fc_mount/subdir
# 应失败并返回 Operation not supported 或类似错误

mv /tmp/fc_mount/test.txt /tmp/fc_mount/renamed.txt
# 应失败（rename 不支持）
```

### 5. 卸载

```bash
fusermount -u /tmp/fc_mount   # Linux
# 或 umount /tmp/fc_mount
```

## 验收标准对照

| 标准 | 验证方式 |
|------|----------|
| cat 可读取 SDK 已支持的文件 | 步骤 2 |
| echo 可走通写路径 | 步骤 3 |
| 不支持操作返回明确错误 | 步骤 4 |
