# P3-07: 缺陷治理与回归测试轨道

## 阶段与优先级

Phase J — 性能、可观测与质量 | P2

## 依赖

- [P1-15D](./P1-15d-write-path-e2e.md)

## 描述

建立长期维护的质量轨道，而不是把它作为阻塞主产品能力的核心功能 issue。

## 交付物

- `docs/bug-triage.md`
- `docs/bug-fix-template.md`
- `tests/regression/`
- `ctest -L regression` 标签约定

## 验收标准

- [ ] 回归测试框架可运行，即使初始用例数量很少。
- [ ] `ctest -L regression` 可单独执行。
- [ ] 缺陷分级标准和修复模板可被后续 issue 复用。

## 涉及目录

```text
docs/
tests/regression/
```
