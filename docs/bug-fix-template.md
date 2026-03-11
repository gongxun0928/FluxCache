# Bug Fix Template

> 供 bug 修复 issue/PR 复用。

## 1. 摘要

- **Issue**: `P3-XX` 或 issue 链接
- **分级**: P0 / P1 / P2
- **现象**: 一句话描述 bug 表现

## 2. 根因

- 简要说明导致 bug 的根本原因

## 3. 修复方案

- 修改点与实现思路

## 4. 验收

- [ ] 主路径验证通过
- [ ] 回归测试已添加（若适用）：`tests/regression/xxx_test.cpp`，标签 `regression`
- [ ] `ctest -L regression` 通过（若新增回归用例）

## 5. 影响范围

- 受影响模块、接口、配置
