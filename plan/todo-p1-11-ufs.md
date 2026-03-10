# Todo: P1-11 UFS 抽象层与 LocalFS 驱动

> 关联: plan/plan_p1-11-ufs.md

## Todo 列表

| ID | 任务 | 状态 | DoD |
|----|------|------|-----|
| T1 | 补充 Status::InvalidArgument | done | status.h/cpp 可构造 InvalidArgument，单元测试通过 |
| T2 | 实现 src/ufs/ufs.h | done | FileStatus、UFS 接口声明，可被 local_ufs 继承 |
| T3 | 实现 local_ufs.h/.cpp | done | LocalFS 实现全部 7 个接口，路径穿越防护 |
| T4 | 实现 ufs_factory.h/.cpp | done | CreateUFS("local", root, out) 返回 LocalFS |
| T5 | 实现 tests/ufs/local_ufs_test.cpp | done | 覆盖创建/读/写/删、GetStatus、List、路径不存在 |
| T6 | 更新 CMakeLists | done | ufs 库与 ufs_test 可构建，ctest -R ufs 通过 |

## 执行顺序

T1 → T2 → T3 → T4 → T5 → T6

## 当前 in_progress

无（按顺序推进）
