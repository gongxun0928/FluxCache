# Plan: P1-11 UFS 抽象层与 LocalFS 驱动

> Status: Done

## Goal

- **Problem**: 项目需要 UFS 抽象层以支持 Phase 1 的 LocalFS 读写、List、GetStatus、mtime 校验。
- **Target outcome**: `src/ufs/` 提供 UFS 接口、FileStatus、LocalFS 实现、UFS Factory；测试在临时目录下验证创建/读/写/删、GetStatus、List、路径不存在错误处理；构建与 ctest 通过。

## 输入

- [design/ufs-design.md](../design/ufs-design.md)
- [issues/P1-11-ufs-localfs.md](../issues/P1-11-ufs-localfs.md)

## Steps

1. 创建 `plan/todo-p1-11-ufs.md`，拆解实现任务与 DoD。
2. 实现 `src/ufs/ufs.h`：FileStatus 结构体、UFS 抽象接口（Read, Write, GetStatus, List, Delete, Rename, Mkdirs）。
3. 实现 `src/ufs/local_ufs.h`、`src/ufs/local_ufs.cpp`：LocalFS 类，基于 `std::filesystem` 或 POSIX。
4. 实现 `src/ufs/ufs_factory.h`、`src/ufs/ufs_factory.cpp`：CreateUFS(scheme, authority, out)。
5. 实现 `tests/ufs/local_ufs_test.cpp`：覆盖验收标准（创建/读/写/删、GetStatus、List、路径不存在）。
6. 更新 `src/ufs/CMakeLists.txt`：移除 stub，添加 ufs.h、local_ufs、ufs_factory 源文件。
7. 更新 `tests/CMakeLists.txt`：添加 ufs 子目录（若不存在）。
8. 执行构建与测试：`cd build && cmake --build . && ctest -R ufs -C Debug --output-on-failure`。
9. 单独 commit：`feat(ufs): add UFS abstraction and LocalFS driver`。

## Risks & Assumptions

- **Risk**: Status 类缺少 `InvalidArgument` 工厂方法。**Mitigation**: 在 `common/status.h/.cpp` 中补充。
- **Assumption**: LocalFS 根路径由调用方传入，测试使用 `std::filesystem::temp_directory_path()`。

## To Confirm

- [x] FileStatus 字段与 design 一致：exists, is_directory, size, mtime_ms, path。
- [x] GetStatus 路径不存在时返回 OK + exists=false，不返回 NotFound。

## 变更分级

P1 — 公共接口、FileStatus 数据结构、单服务核心逻辑。
