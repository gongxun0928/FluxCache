# P3-02: PageStore 并发优化

## 阶段与优先级

Phase J — 性能、可观测与质量 | P1

## 依赖

- [P1-10](./P1-10-page-store.md)

## 描述

优化 `PageStore` 的锁粒度与并发吞吐。推荐基线方案是两级分段锁，但允许使用等价方案。

## 交付物

- 并发控制重构
- 读写基准测试
- TSAN 或等价并发正确性验证

## 验收标准

- [ ] 并发读吞吐相比粗粒度锁有明确提升。
- [ ] 混合读写延迟有可量化改善。
- [ ] 无数据竞争。
- [ ] 既有功能回归测试通过。

## 设计参考

- [Master 元数据设计](../design/metadata-design.md)

## 涉及目录

```text
src/worker/page/
tests/worker/
tests/benchmark/
```
# P3-02: PageStore 并发优化

## 阶段与优先级

Phase J — 性能、可观测与质量 | P1

## 依赖

- [P1-10](./P1-10-page-store.md)

## 描述

优化 `PageStore` 的锁粒度与并发吞吐。推荐基线方案是两级分段锁，但允许使用等价方案。

## 交付物

- 并发控制重构
- 读写基准测试
- TSAN 或等价并发正确性验证

## 验收标准

- [ ] 并发读吞吐相比粗粒度锁有明确提升。
- [ ] 混合读写延迟有可量化改善。
- [ ] 无数据竞争。
- [ ] 既有功能回归测试通过。

## 设计参考

- [Master 元数据设计](../design/metadata-design.md)

## 涉及目录

```text
src/worker/page/
tests/worker/
tests/benchmark/
```
