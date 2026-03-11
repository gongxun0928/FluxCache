# Review: P3-04 零拷贝与减少复制路径

> 日期: 2026-03-11
> 关联: plan/plan_p3-04-zero-copy.md, design/zero-copy-analysis.md

## 评审结论

- **结论**: 需修订（因项目预存构建问题，无法完整执行 Testing Gate）
- **风险等级**: 低

## 发现列表

| 严重度 | 发现 | 建议动作 |
|--------|------|----------|
| 中 | 项目存在 tier_evictor/tier_promoter 与 worker_server 的接口不一致，导致构建失败 | 需项目维护者先修复预存构建问题 |
| 低 | 单页快速路径已正确实现，逻辑等价于原多页路径 | 无 |
| 低 | Benchmark 依赖 fluxcache_client，若 client 链接失败则无法运行 | 构建修复后可验证 |

## 变更摘要

- **design/zero-copy-analysis.md**: 新增，完整列出优化前复制路径、候选方案、选定方案 A
- **plan/plan_p3-04-zero-copy.md**: 新增，执行计划
- **plan/todo-p3-04-zero-copy.md**: 新增，Todo 与 DoD
- **src/worker/worker_service_impl.cpp**: ReadPages 与 BatchReadPages 单页快速路径，消除 `concatenated += page_data` 拷贝
- **tests/benchmark/single_page_read_bench.cpp**: 新增，单页读基准
- **tests/benchmark/CMakeLists.txt**: 新增 single_page_read_bench 目标

## 残余风险与测试缺口

- 构建未通过，无法执行 ctest 与 benchmark
- 单页路径与多页路径的 UFS fallback、mtime 校验、stale 逻辑已保持一致，代码审查通过
