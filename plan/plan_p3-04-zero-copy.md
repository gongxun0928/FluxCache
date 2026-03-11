# Plan: P3-04 零拷贝与减少复制路径

> 状态: In Progress
> 关联: design/zero-copy-analysis.md, issues/P3-04-zero-copy.md
> 变更分级: P1

## Goal

完成读路径复制点分析，落地至少一处热路径优化，提供基准对比。

## Steps

1. **设计文档**（已完成）
   - 产出 `design/zero-copy-analysis.md`
   - 明确优化前复制路径、候选方案、选定方案 A（Worker 单页快速路径）

2. **实现 Worker ReadPages 单页快速路径**
   - 文件：`src/worker/worker_service_impl.cpp`
   - 当 `page_indices().size() == 1` 时，直接 `response->set_data(std::move(page_data))`，跳过 `concatenated`
   - 同步修改 BatchReadPages 内单页 request 的等效逻辑

3. **单元测试**
   - 确保现有 `ReadPagesTest` 全部通过
   - 可选：补充单页快速路径的显式用例（若现有用例已覆盖则不必新增）

4. **基准对比**
   - 新增或扩展 benchmark：单页读场景（多 block、每 block 1 页）
   - 输出优化前后吞吐/延迟数据

5. **评审与测试**
   - 按 `review-evidence-checklist.mdc` 形成评审产物
   - 按 `testing-gate-no-deploy.mdc` 执行构建、测试、lint

## 变更分级

- P1：涉及 Worker 核心读路径，单审即可（可抽样双审）

## 验证动作

- [ ] `cd build && cmake --build .` 成功
- [ ] `cd build && ctest --output-on-failure` 通过
- [ ] 基准脚本可运行并输出前后对比
- [ ] Lint 无新增问题

## Risks

- 单页与多页分支逻辑需保持一致（UFS fallback、mtime 校验）
