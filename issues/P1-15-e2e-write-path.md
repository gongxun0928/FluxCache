# P1-15: 已拆分

`P1-15` 不再作为活跃 issue 使用。

为避免一个“集成 issue”同时承担 Master、Worker、Client 三侧写路径主逻辑实现，现已拆分为：

- [P1-15A](./P1-15a-master-create-and-complete-file.md)：Master.CreateFile / CompleteFile
- [P1-15B](./P1-15b-worker-write-pages.md)：Worker.WritePages 最小实现
- [P1-15C](./P1-15c-client-write-path.md)：Client.Write 最小实现
- [P1-15D](./P1-15d-write-path-e2e.md)：写路径端到端验证

后续请以拆分后的子 issue 为准。
