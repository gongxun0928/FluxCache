# P1-05: 已拆分

`P1-05` 不再作为活跃 issue 使用。

为避免一个 issue 同时承担进程骨架、RocksDB 元数据、Worker 状态机和 hash ring 四类职责，现已拆分为：

- [P1-05A](./P1-05a-master-server-bootstrap.md)：Master 进程启动与服务注册骨架
- [P1-05B](./P1-05b-inode-store-and-tree.md)：InodeStore 与 InodeTree 最小持久化
- [P1-05C](./P1-05c-worker-manager-and-hash-ring.md)：WorkerManager 与 HashRingManager 最小实现

后续请以拆分后的子 issue 为准。
