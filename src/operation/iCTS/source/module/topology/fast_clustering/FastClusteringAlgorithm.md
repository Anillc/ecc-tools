# Fast Clustering Algorithm

本文档描述 iCTS `fast_clustering` 模块当前实现的算法、接口契约、约束语义、复杂度特征和 benchmark 输出。
对应实现位于：

- `FastClustering.hh`
- `FastClustering.cc`
- `FastClusteringInternal.hh`
- facade: `clustering/Clustering.*`, `TopologyGen.*`
- default source flow: `flow/synthesis/topology/Topology.cc`
- benchmark: `test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkTest.cc`

## 目标

`fast_clustering` 是当前 CTS sink clustering 的生产实现。它直接在二维 DBU 坐标空间构造局部 cluster，并通过共享的
cluster constraint/electrical evaluator 做最终合法性验证。

当前流程：

```text
routing-cap-aware recursive spatial bisection
  -> cap-aware small-cluster neighbor merge
  -> boundary load polish
  -> final constraint and electrical evaluation
  -> longest-axis repair split for failed clusters
```

这个流程同时考虑两个尺度：

- 全局性：递归按当前 bbox 最长轴切分，使聚类先跟随整体 sink 分布和长宽形态。
- 局部性：小簇合并阶段优先选择空间邻近且合并后仍合法的 neighbor，边界 polish 会把 routing-cap-heavy cluster 的边界
  sink 尝试移动到邻近 light cluster，以降低 cluster 间 total routing-cap proxy 方差。

## 接口契约

`FastClustering` 对外接口：

```cpp
static auto buildElectricalBaseConfig(std::size_t max_fanout, double max_cap) -> ClusterConfig;
static auto runDefault(const std::vector<Pin*>& loads, const ClusterConfig& base_config) -> ClusterResult;
static auto run(const std::vector<Pin*>& loads, const ClusterConfig& config) -> ClusterResult;
```

facade 入口包括：

```cpp
Clustering::fastClustering(...)
Clustering::defaultFastClustering(...)
TopologyGen::fastClustering(...)
TopologyGen::defaultFastClustering(...)
TopologyGen::buildFastClusteringElectricalConfig(...)
```

默认 CTS source flow 使用：

```cpp
auto clustering_config = TopologyGen::buildFastClusteringElectricalConfig(...);
auto cluster_result = TopologyGen::defaultFastClustering(sinks, clustering_config);
```

输入：

- `std::vector<Pin*> loads`
- `ClusterConfig config`

输出：

- `ClusterResult::clusters`
- `ClusterResult::centers`
- `ClusterResult::electrical_summaries`

输出必须满足：

- 每个非空输入 `Pin*` 最多且恰好出现一次。
- `centers.size() == clusters.size()`。
- `electrical_summaries.size() == clusters.size()`。
- 空输入返回空 `ClusterResult`。
- 全空或无有效 `Pin*` 输入返回空结果，并输出 warning。

## 约束语义

`fast_clustering` 使用 `ClusterConfig` 和 `ClusterConstraintEvaluator` 做最终合法性检查。

### Fanout

```text
config.max_fanout > 0: cluster.size() <= max_fanout
config.max_fanout == 0: final evaluator does not limit fanout
```

实现细节：

- 初始 packing 阶段仍会使用一个内部 fanout 上限，以避免无 fanout 限制时产生过大的 cluster。
- 当 `max_fanout == 0` 时，该内部上限为 `kDefaultPackingFanout = 32`。
- 这个内部上限用于 runtime 和局部质量控制，不改变最终合法性定义。

### Diameter

diameter 定义为 cluster bbox 的 Manhattan span：

```text
diameter = (max_x - min_x) + (max_y - min_y)
```

最终约束：

```text
config.max_diameter > 0: diameter <= max_diameter
config.max_diameter <= 0: no diameter limit
```

递归二分阶段会提前使用这个 bbox diameter 作为几何合法性过滤条件。

### Capacitance / Routing

最终 cluster 通过 `ClusterConstraintEvaluator::evaluateLoads(...)` 评估：

- fanout
- diameter
- pin-cap lower bound
- exact cap/routing summary, when enabled

如果最终评估失败：

- 对非 singleton cluster 执行最长轴二分修复。
- 对修复后的子簇递归重新评估。
- singleton 仍非法时记录 warning，并让本轮 fast 结果失败。

### Root Policy

final evaluation root 按 config 解析：

- `ClusterRootPolicy::kCenter`: cluster 几何中心
- otherwise: cluster 坐标 median

输出 `ClusterResult::centers` 当前统一使用 rounded geometric center。

### Scoring Proxy

fast 在草稿阶段使用轻量 score proxy，避免在 split / merge / boundary polish 中频繁 exact routing：

```text
kTotalWirelength: wirelength_weight * bbox_diameter
kMaxDiameter: bbox_diameter
zero-diameter cluster: max_diameter as non-zero penalty, when max_diameter > 0
```

total routing-cap proxy 定义为：

```text
root = config.root_policy == kCenter ? rounded geometric center : coordinate median
routing_cap_proxy(cluster) = sum(|sink.x - root.x| + |sink.y - root.y|)
```

它不是 exact RC cap，而是一个低成本的 routing-cap proxy。benchmark 同样用这个 proxy 计算 cluster 间方差，避免把 exact
routing 打开后让 benchmark 被 routing runtime 主导。final legality 仍由 `ClusterConstraintEvaluator` 负责。

## 核心数据结构

### LoadEntry

```cpp
struct LoadEntry {
  Pin* pin;
  Point<int> location;
  std::size_t original_index;
};
```

作用：

- 过滤 null pin。
- 缓存 DBU 坐标，减少重复访问。
- 保留输入顺序，用于 deterministic tie-break。

### Bounds

```cpp
struct Bounds {
  int min_x;
  int min_y;
  int max_x;
  int max_y;
};
```

作用：

- 维护 cluster bbox。
- 快速计算 diameter。
- 快速判断两个 draft merge 后是否几何合法。

### ClusterDraft

```cpp
struct ClusterDraft {
  std::vector<std::size_t> entry_ids;
  Bounds bounds;
  double routing_cap_proxy;
  bool active;
};
```

作用：

- 表示尚未 final electrical evaluation 的草稿 cluster。
- 使用 `entry_ids` 引用 `LoadEntry`，避免早期频繁搬动 `Pin*`。
- 缓存 `routing_cap_proxy`，让 split/merge/move 的 cap 方差目标不需要反复访问 `Pin` 或触发 exact routing。
- merge 阶段通过 `active=false` 延迟删除无效 draft。

## 算法流程

1. `CollectEntries(loads)` 过滤 null pin，并生成 `LoadEntry`。如果 `entries` 为空，直接返回空结果。
2. `BuildSpatialRecursiveClusters(entries, config)` 按最长轴递归切分。切分点在理想位置附近的小窗口中选择，同时考虑几何
   compactness 和 leaf-level routing-cap proxy balance。
3. `PolishSmallClusters(drafts, entries, config)` 固定执行少量轮次，优先尝试把 singleton 和小簇合并到近邻合法 cluster。
4. `PolishBoundaryLoads(drafts, entries, config)` 对 routing-cap-heavy cluster 尝试向邻近 light cluster 移动少量边界
   sink。
5. `FinalizeClusters(drafts, entries, config)` materialize cluster，运行最终合法性评估；失败 cluster 通过最长轴 repair split
   递归修复。
6. `CountAssignedLoads(finalized) == entries.size()` 做完整性检查。失败时记录 warning 并返回空结果，调用侧据此决定后续处理。

## Pseudocode

```text
FastClustering::run(loads, config):
  if loads.empty:
    return empty ClusterResult

  entries = CollectEntries(loads)
  if entries.empty:
    warn
    return empty ClusterResult

  drafts = BuildSpatialRecursiveClusters(entries, config)
  PolishSmallClusters(drafts, entries, config)

  finalized = FinalizeClusters(drafts, entries, config)
  if finalized missing or assigned_load_count != entries.size:
    warn
    return empty ClusterResult

  log loads / clusters / strategy
  return finalized
```

## 复杂度

设：

- `n` = valid load count
- `f` = packing fanout limit
- `k` = draft cluster count, roughly `ceil(n / f)`
- `r` = merge round count, currently 2

输入归一化为 `O(n)`。

递归空间二分在每个递归节点对当前子集排序。平衡情况下，所有层级总成本近似：

```text
O(n log^2 n)
```

小簇合并和 boundary polish 都只对 bounded nearest candidates 做真实 draft/objective 计算；当前主要成本来自 active draft
之间的距离排序：

```text
O(r * k^2 log k)
```

最终评估：

```text
O(k * ClusterConstraintEvaluator(f)) + repair overhead
```

当 exact cap/routing enabled 时，最终评估是主要成本来源。fast 的关键 runtime 优势是把 exact work 限制在最终 cluster 和
少量 repair cluster 上，而不是在大量候选窗口中反复执行。

## Benchmark

fast real-tech benchmark 是 fast-only benchmark。它验证每个 case 的 fast 结果合法性，并输出：

- per-case runtime
- per-case score
- cluster count and load coverage
- routing-cap proxy variance
- per-case cluster SVG
- aggregate summary report

benchmark 输出目录：

```text
icts_test_output/clustering/
```

## 设计取舍

### 为什么直接使用二维空间 partition

CTS clustering 的输入是带 DBU 坐标的 sink 集合。直接在二维空间构造 partition 可以减少候选数量，并让局部合并与边界移动
自然使用 bbox / distance / routing-cap proxy 等低成本度量。

### 为什么 merge 只做固定 2 轮

merge 是 polish，不是核心搜索。固定小轮数可以限制 runtime，并避免退化成 graph agglomeration。当前 benchmark 中 2 轮足以
消除大部分递归过切分带来的小簇问题。

### 为什么 routing cap 用 proxy 而不是 exact cap

cluster 间 total routing cap 方差是一个全局统计目标。如果在 draft split/merge 的每个候选上都调用 exact routing，会把
fast 算法重新拖回候选搜索型 runtime。当前实现用 root-to-sink Manhattan sum 表示 routing-cap proxy：它不能替代 final
electrical evaluation，但足够在局部搜索中判断哪一类 cluster 明显 cap-heavy / cap-light。

## 已知限制

1. 初始 longest-axis split 仍可能产生轴向边界 artifact；boundary polish 只能处理邻近且局部可改善的边界 sink。
2. merge/boundary neighbor 当前仍需要扫描 active drafts 来排序 nearest candidates，cluster 数很大时可进一步替换为 spatial
   bucket / k-nearest graph。
3. draft 阶段没有维护 pin-cap lower bound，因此 pin-cap 失败主要在 final evaluation/repair 中处理。
4. `max_fanout == 0` 时仍用 32 作为内部 packing fanout，可能产生比理论最少 cluster 更多的结果。
