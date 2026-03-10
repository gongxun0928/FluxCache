# P1-01: 项目脚手架与构建系统

## 阶段与优先级

Phase A — 契约冻结 | P1

## 依赖

无

## 描述

建立 FluxCache 的基础目录结构、CMake 构建系统和测试入口，为后续所有 issue 提供统一工程骨架。

## 交付物

- 顶层与子目录 `CMakeLists.txt`
- `src/{common,master,worker,client,ufs,proto}/`
- `tests/`
- `config/`
- 基础第三方依赖接入骨架

## 验收标准

- [ ] `cmake .. && cmake --build .` 成功。
- [ ] `ctest --output-on-failure` 可运行。
- [ ] 各模块静态库可生成。
- [ ] 可选功能开关关闭时，对应模块不参与编译。

## 涉及目录

```text
CMakeLists.txt
src/
tests/
config/
```
