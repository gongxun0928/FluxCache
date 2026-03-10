# P2-02: 自动层级晋升与淘汰流程

## 阶段与优先级

Phase F — 多层缓存与淘汰 | P1

## 依赖

- [P2-01](./P2-01-storage-tier-ssd-hdd.md)
- [P2-03](./P2-03-eviction-policy-lru.md)
- [P2-05](./P2-05-metastore-rocksdb.md)

## 描述

实现多层缓存下的晋升与淘汰执行流程。

本 issue 只负责“何时搬迁、怎样更新索引、如何不破坏主路径”，不再把策略定义、恢复闭环和传输优化混在一起。

## 交付物

- `src/worker/cache/tier_promoter.h/.cpp`
- `src/worker/cache/tier_evictor.h/.cpp`
- 与 PageStore / MetaStore 的索引联动
- 可配置的后台巡检

## 验收标准

- [ ] 在可控测试负载下，热页可从低速层晋升到高速层。
- [ ] 容量超阈值时，冷页按策略被降级或驱逐。
- [ ] 搬迁前后 PageStore / MetaStore 索引保持一致。
- [ ] 测试使用伪时钟或直接触发接口验证，不依赖后台线程偶发命中。

## 涉及目录

```text
src/worker/cache/
tests/worker/
tests/integration/
```
