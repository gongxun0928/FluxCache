# Bug Triage & Regression Test Track Design

> P3-07: 缺陷治理与回归测试轨道

## 1. 目标

建立长期维护的质量轨道，作为 Phase J 可观测与质量的一部分，而非阻塞主产品能力的核心功能。

## 2. 范围

- **缺陷分级标准**：用于后续 bug 修复 issue 的优先级与处理方式。
- **修复模板**：统一 bug 修复的文档格式与验收标准。
- **回归测试轨道**：`tests/regression/` 目录 + CTest `regression` 标签，支持 `ctest -L regression` 单独执行。

## 3. 缺陷分级标准（与 change-tiering 对齐）

| 等级 | 含义 | 示例 |
|------|------|------|
| `P0` | 生产崩溃、数据丢失、安全漏洞 | 核心路径崩溃、数据损坏 |
| `P1` | 核心功能异常、重要性能退化 | 读/写路径错误、明显性能回退 |
| `P2` | 次要功能、边界条件、文档/注释 | 非关键路径、文档错误 |

## 4. 回归测试设计

- **目录**：`tests/regression/`
- **标签**：`LABELS regression`，通过 `ctest -L regression` 执行。
- **初始用例**：至少一个占位测试（如 `regression_smoke_test`），验证框架可运行。
- **后续用例**：每个 bug 修复可在此目录增加对应回归用例，并打上 `regression` 标签。

## 5. 风险与假设

- **风险**：无，P2 低风险，仅文档与测试框架。
- **假设**：后续 bug 修复 issue 会引用 `docs/bug-triage.md` 和 `docs/bug-fix-template.md`。

## 6. 验收标准

- 回归测试框架可运行，即使初始用例数量很少。
- `ctest -L regression` 可单独执行。
- 缺陷分级标准和修复模板可被后续 issue 复用。

## 7. 非目标

- 不在此 issue 中增加具体 bug 修复用例（仅占位）。
- 不改变现有测试结构或标签。
