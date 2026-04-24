# Fast Clustering Algorithm

本文档描述 iCTS `fast_clustering` 模块当前实现的算法、接口契约、约束语义、复杂度特征和与
`linear_clustering` 的主要差异。对应实现位于：

- `FastClustering.hh`
- `FastClustering.cc`
- facade: `clustering/Clustering.*`, `TopologyGen.*`
- default source flow: `flow/synthesis/ClockSynthesis.cc`
- benchmark: `test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkTest.cc`

## 目标

`fast_clustering` 的目标是在保持 `linear_clustering` 输入输出契约和 CTS 聚类设计约束一致的前提下，减少
`linear_clustering` 中多策略、多 offset sweep、重复候选评估带来的 runtime 成本。

当前实现采用：

```text
routing-cap-aware 递归空间二分
  -> cap-aware 小簇邻近合并
  -> boundary load polish
  -> 最终约束评估
  -> 失败簇最长轴修复切分
  -> 必要时回退 linear
```

这个流程同时考虑两个尺度：

- 全局性：递归按当前 bbox 最长轴切分，使聚类先跟随整体 sink 分布和长宽形态。
- 局部性：小簇合并阶段优先选择空间邻近且合并后仍合法的 neighbor，边界 polish 会把
  routing-cap-heavy cluster 的边界 sink 尝试移动到邻近 light cluster，以降低 cluster 间
  total routing-cap 方差。

## 接口契约

`FastClustering` 对外保持与 `LinearClustering` 同形的调用接口：

```cpp
static auto buildElectricalBaseConfig(std::size_t max_fanout, double max_cap) -> LinearClusteringConfig;
static auto runDefault(const std::vector<Pin*>& loads, const LinearClusteringConfig& base_config) -> ClusterResult;
static auto run(const std::vector<Pin*>& loads, const LinearClusteringConfig& config) -> ClusterResult;
```

facade 入口包括：

```cpp
Clustering::fastClustering(...)
Clustering::defaultFastClustering(...)
TopologyGen::fastClustering(...)
TopologyGen::defaultFastClustering(...)
TopologyGen::buildFastClusteringElectricalConfig(...)
```

默认 CTS source flow 已切到 fast clustering：

```cpp
auto clustering_config = TopologyGen::buildFastClusteringElectricalConfig(...);
auto cluster_result = TopologyGen::defaultFastClustering(sinks, clustering_config);
```

linear facade 保留为显式对比、回归和 fallback 使用。

输入：

- `std::vector<Pin*> loads`
- `LinearClusteringConfig config`

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

`fast_clustering` 复用 `LinearClusteringConfig` 和 `ConstraintEvaluator`，因此最终合法性与 linear 使用同一套核心规则。

### Fanout

最终约束：

```text
config.max_fanout > 0: cluster.size() <= max_fanout
config.max_fanout == 0: final evaluator 不限制 fanout
```

实现细节：

- 初始 packing 阶段仍会使用一个内部 fanout 上限，以避免无 fanout 限制时产生过大的 cluster。
- 当 `max_fanout == 0` 时，该内部上限为 `kDefaultPackingFanout = 32`。
- 这个内部上限用于 runtime 和局部质量控制，不改变最终合法性定义。

### Diameter

diameter 与 linear 保持一致，定义为 cluster bbox 的 Manhattan span：

```text
diameter = (max_x - min_x) + (max_y - min_y)
```

最终约束：

```text
config.max_diameter > 0: diameter <= max_diameter
config.max_diameter <= 0: 不限制 diameter
```

递归二分阶段会提前使用这个 bbox diameter 作为几何合法性过滤条件。

### Capacitance / Routing

最终 cluster 通过 `ConstraintEvaluator::evaluateLoads(...)` 评估：

- fanout
- diameter
- pin-cap lower bound
- exact cap/routing summary, when enabled

当前 fast 实现的 exact 触发条件与 `LinearClustering::run` 语义对齐：

```cpp
need_exact_cap = config.enable_exact_cap;
```

`ConstraintEvaluator` 内部再根据 `max_cap` 和 `always_build_exact_cap` 决定是否执行 exact eval。

如果最终评估失败：

- 对非 singleton cluster 执行最长轴二分修复。
- 对修复后的子簇递归重新评估。
- singleton 仍非法时记录 warning，并让本轮 fast 结果失败。

### Root Policy

final evaluation root 按 config 解析：

- `LinearRootPolicy::kCenter`: cluster 几何中心
- otherwise: cluster 坐标 median

输出 `ClusterResult::centers` 当前统一使用 rounded geometric center，与 linear materialization 语义一致。

### Scoring Proxy

fast 在草稿阶段使用轻量 score proxy，避免在 split / merge / boundary polish 中频繁 exact routing：

```text
kTotalWirelength: wirelength_weight * bbox_diameter
kMaxDiameter: bbox_diameter
zero-diameter cluster: max_diameter 作为非零 penalty, when max_diameter > 0
```

新增的 total routing-cap proxy 定义为：

```text
root = config.root_policy == kCenter ? rounded geometric center : coordinate median
routing_cap_proxy(cluster) = sum(|sink.x - root.x| + |sink.y - root.y|)
```

它不是 exact RC cap，而是一个低成本的 routing-cap proxy：在相同 routing layer / wire width 前提下，wire cap
与 routed length 一阶正相关，因此用 star wirelength proxy 作为 draft 阶段的 cap 均衡目标。benchmark 同样用这个
proxy 计算 cluster 间方差，避免把 exact routing 打开后让 benchmark 被 routing runtime 主导。

方差惩罚使用同量纲的归一化平方项：

```text
variance_penalty(proxy, target) = (proxy - target)^2 / max(1, target)
```

split、merge 和 boundary polish 会把几何 score 与该惩罚组合。这样做比只看 bbox diameter 更能约束“有些 cluster
routing cap 很重、有些 cluster routing cap 很轻”的情况，同时仍然保持局部搜索很轻。

最终 benchmark score 仍基于输出 `ClusterResult` 重新计算，与 linear/fast 使用同一套评测逻辑。

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

### 1. 输入归一化

`CollectEntries(loads)` 过滤 null pin，并生成 `LoadEntry`：

```text
for each input load:
  if load != nullptr:
    entries.push_back({pin, location, original_index})
```

如果 `entries` 为空，直接返回空结果。

### 2. 初始递归空间二分

入口：

```cpp
BuildSpatialRecursiveClusters(entries, config)
```

先解析 packing fanout：

```text
fanout_limit = min(config.max_fanout, load_count), if max_fanout > 0
fanout_limit = min(32, load_count), if max_fanout == 0
```

递归规则：

```text
BuildSpatialRecursiveClusters(entry_ids):
  bounds = CalcClusterBounds(entry_ids)

  if entry_ids.size <= fanout_limit and diameter(bounds) legal:
    emit draft cluster
    return

  if entry_ids.size == 1:
    emit singleton draft
    return

  sort entry_ids by current bbox longest axis
  split_size = ResolveRecursiveSplitSize(...)
  recurse(left half)
  recurse(right half)
```

最长轴排序：

- 如果 bbox width >= height，按 `x, y, original_index` 排序。
- 否则按 `y, x, original_index` 排序。

`ResolveRecursiveSplitSize` 会根据目标 cluster 数估计二分比例，而不是简单固定 50/50：

```text
target_cluster_count = ceil(entry_count / fanout_limit)
if target_cluster_count <= 1 and diameter illegal:
  target_cluster_count = 2
left_cluster_count = max(1, target_cluster_count / 2)
ideal_split_size = ceil(entry_count * left_cluster_count / target_cluster_count)
```

实际 split 不是只用 `ideal_split_size`，而是在其左右 `kSplitCandidateWindow = 4` 的窗口内选择：

```text
for candidate split near ideal:
  lhs = draft(candidate left)
  rhs = draft(candidate right)
  lhs_expected_leaf_count = expected child cluster count(lhs)
  rhs_expected_leaf_count = expected child cluster count(rhs)
  target_leaf_proxy = (lhs.routing_cap_proxy + rhs.routing_cap_proxy) /
                      (lhs_expected_leaf_count + rhs_expected_leaf_count)
  score = geometry(lhs) + geometry(rhs) +
          split_weight * weighted_variance_penalty(lhs_leaf_avg_proxy, rhs_leaf_avg_proxy, target_leaf_proxy)
choose lowest score, tie-break by distance to ideal split
```

这个 split objective 的重点不是让左右子树总 cap 完全一样，而是让左右子树的“预计 leaf cluster 平均 routing cap”
更接近同一个 target。这样在 `target_cluster_count` 为奇数或左右子树会继续递归时，不会错误地要求一个预计会产生两簇的
子树和另一个只产生一簇的子树具有相同 total proxy。

这样做的目的：

- fanout 主导时，分裂数量接近理论最少 cluster count。
- diameter 主导时，即使 fanout 合法也会继续分裂。
- 递归在全局 bbox 上工作，优先处理整体空间分布。
- split 同时看局部 geometry 和 leaf-level routing-cap balance，避免早期切分制造明显 cap-heavy leaf。

### 3. 小簇邻近合并

入口：

```cpp
PolishSmallClusters(drafts, entries, config)
```

固定最多执行 `kMergeRoundCount = 2` 轮。

每轮先按 cluster size 从小到大遍历，让 singleton 和小簇优先获得合并机会：

```text
sort cluster ids by draft.entry_ids.size()
for each small active cluster:
  collect nearest active neighbor candidates, bounded by kMaxMergeNeighborCandidates
  for each legal merge candidate:
    compare cap-aware merged objective and cap-aware separate objective
  merge if:
    merged remains legal, and
    merged objective does not degrade, or one side is singleton
```

neighbor 距离使用 bbox gap：

```text
CalcBoundsDistance(cluster.bounds, neighbor.bounds)
```

merge 合法性使用：

- fanout legal
- diameter legal

merge objective 使用当前 active draft 的平均 `routing_cap_proxy` 作为 target。合并后 cluster 数变化，因此会重新估计
after-target：

```text
before_target = sum(active.routing_cap_proxy) / active_count
after_target = (sum - lhs.proxy - rhs.proxy + merged.proxy) / (active_count - 1)
separate = objective(lhs, before_target) + objective(rhs, before_target)
merged = objective(merged, after_target)
```

此阶段不做 exact cap/routing，主要目的是用很低成本减少初始递归二分造成的小簇碎片，同时避免把两个已经接近 target 的
cluster 合成一个新的 cap-heavy cluster。

### 4. Boundary Load Polish

入口：

```cpp
PolishBoundaryLoads(drafts, entries, config)
```

固定最多执行 `kBoundaryPolishRoundCount = 2` 轮。每轮按 `routing_cap_proxy` 从高到低访问 cluster：

```text
for each cap-heavy source cluster:
  find nearby cap-light target clusters, bounded by kMaxBoundaryNeighborCandidates
  candidate moved loads = source loads closest to target bbox and farthest from source root
  try moving one candidate load source -> target
  accept the best move if:
    source_after and target_after are fanout/diameter legal
    pair objective with routing-cap variance penalty improves
```

这个阶段不改变 cluster 数，只在边界附近移动少量 sink。它的作用是修正最长轴 split 的边界 artifact：当一个边界 sink
让某个 cluster routing cap proxy 明显偏重，而邻近 cluster 偏轻时，用一次局部移动降低两者相对全局 target 的方差。

### 5. Materialize

`MaterializeCluster(draft, entries)` 将 draft 转为 `std::vector<Pin*>`。

为了 deterministic output，cluster 内 pins 按 `Pin::get_name()` 排序。

### 6. 最终约束评估与修复切分

入口：

```cpp
FinalizeClusters(drafts, entries, config)
```

每个 draft materialize 后进入：

```cpp
AppendFinalCluster(cluster, config, evaluator, result)
```

流程：

```text
root = ResolveEvaluationRoot(cluster, config)
evaluation = evaluator.evaluateLoads(cluster, root, config, enable_exact_cap)

if evaluation.legal:
  append cluster, center, electrical summary
  return true

if cluster.size <= 1:
  warn and fail

split cluster by longest axis median
AppendFinalCluster(lhs)
AppendFinalCluster(rhs)
```

这个修复过程保证：

- fanout/diameter/cap/routing 失败不会直接输出非法 cluster。
- 每次 split 都严格减少 cluster size，因此递归可终止。
- 与 initial split 相同，repair split 也优先沿最长轴，保持空间局部性。

### 7. 完整性检查与 fallback

finalize 后做完整性检查：

```text
CountAssignedLoads(finalized) == entries.size()
```

如果 finalization 失败或 load 数不完整：

```cpp
LOG_WARNING << "Fast clustering failed ... Falling back to linear clustering.";
return LinearClustering::run(loads, config);
```

fallback 的设计意图：

- benchmark 和未来 CTS flow 更需要合法完整 partition，而不是非法 fast output。
- 当某些 corner case 触发 fast 修复失败时，仍保持 linear 级别的功能可用性。
- fallback 会牺牲该 case runtime，但避免 correctness regression。

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
  PolishSmallClusters(drafts, entries, config)  // cap-aware merge + boundary polish

  finalized = FinalizeClusters(drafts, entries, config)
  if finalized missing or assigned_load_count != entries.size:
    warn
    return LinearClustering::run(loads, config)

  log loads / clusters / strategy
  return finalized
```

## 复杂度

设：

- `n` = valid load count
- `f` = packing fanout limit
- `k` = draft cluster count, roughly `ceil(n / f)`
- `r` = merge round count, currently 2

### 输入归一化

```text
O(n)
```

### 递归空间二分

每个递归节点会对当前子集排序。平衡情况下，所有层级总成本近似：

```text
O(n log^2 n)
```

在实际 fanout 约束下，递归会在 cluster size 接近 `f` 后停止，因此成本受 `f` 和空间分布共同限制。

### 小簇合并

当前实现会为每个 active cluster 计算 bbox gap 并取最近的 `kMaxMergeNeighborCandidates = 12` 个候选。距离排序仍需要看
active draft，但真正构造 merged draft 和 cap-aware objective 只发生在 bounded candidates 上：

```text
O(r * k^2 log k) distance ordering + O(r * k * candidate_count * f) merge objective
```

由于 `k ~= n / f`、`r=2`、`candidate_count=12`，在 benchmark 规模下这部分成本低于 linear 多策略 sweep 的候选评估成本。

### Boundary Polish

boundary polish 最多 2 轮，每个 source 只看最多 8 个邻近 light cluster 和每个 target 最多 8 个边界 load：

```text
O(polish_rounds * k^2 log k) neighbor ordering
+ O(polish_rounds * k * boundary_neighbor_count * boundary_entry_count * f) move objective
```

这部分不做 exact routing，不改变 cluster 数，主要用于降低 cluster 间 total routing-cap proxy 方差。

### 最终评估

每个最终 cluster 评估一次，失败修复会额外评估子簇：

```text
O(k * ConstraintEvaluator(f)) + repair overhead
```

当 exact cap/routing enabled 时，最终评估是主要成本来源。fast 的关键 runtime 优势是把 exact work 限制在最终 cluster 和少量 repair cluster 上，而不是在大量候选窗口中反复执行。

## 与 Linear Clustering 的差异

| 维度 | linear_clustering | fast_clustering |
| --- | --- | --- |
| 全局视角 | 多个 Hilbert/order strategy + offset sweep | 递归最长轴空间二分 |
| 局部性 | 一维 order 上的连续 segment | bbox 空间局部 cluster + 邻近小簇合并 + 边界 sink move |
| 候选数量 | 多 strategy、多 rotation、多 segment candidate | 初始 partition + bounded split/merge/boundary/repair |
| exact eval | 可能在 sweep candidate 中重复触发 | final cluster 和 repair cluster 触发 |
| 质量控制 | 多策略选最小 partition score | 几何 compactness + routing-cap 方差 proxy + final legality |
| 失败策略 | 返回自身搜索结果 | final 失败时 fallback 到 linear |

linear 的优势是搜索更充分，尤其在复杂一维排序能表达 locality 时质量稳定；缺点是候选评估多、runtime 成本高。fast 的优势是直接在二维空间里构造局部 cluster，并避免大规模 sweep；当前版本额外把 total routing-cap proxy
方差纳入 split/merge/boundary objective，因此更容易得到 routing load 更均衡的 cluster 集合。

## 当前 Benchmark 观察

在 20 个 ICS55 placement-stage benchmark case 上，当前实现的最近一次结果为：

```text
case_count=20
legal_case_count=20
linear_total_runtime_ms=1616.96
fast_total_runtime_ms=256.376
runtime_speedup=6.30698
linear_total_score=1.13479e+08
fast_total_score=1.07609e+08
score_improvement_ratio=0.0517282
linear_routing_cap_proxy_variance_sum=1.40197e+12
fast_routing_cap_proxy_variance_sum=2.59427e+11
routing_cap_proxy_variance_improvement_ratio=0.814955
fast_runtime_case_wins=20
fast_score_case_wins=18
fast_routing_cap_variance_case_wins=19
acceptance_runtime=pass
acceptance_score=pass
acceptance_routing_cap_variance=pass
```

benchmark 还会为每个 case 输出 clustering 结构 SVG：

```text
cluster_svgs/case_XX_<case_name>_clusters.svg
```

每个 SVG 左侧为 linear clustering，右侧为 fast clustering，复用现有 cluster visualization 风格。

## 设计取舍

### 为什么不是继续优化 linear sweep

linear 的主要 runtime 成本来自策略组合和 offset sweep。继续优化 sweep 可以降低常数，但算法仍然围绕一维 order 连续 segment 展开。fast 选择直接构造二维空间 partition，减少候选数量和 exact eval 次数。

### 为什么需要 fallback

CTS clustering 是 correctness-first 的模块。fast 的 benchmark 目标是提高 runtime 和 score，但不能输出非法或不完整 partition。fallback 提供保守边界：

- 正常 case 使用 fast output。
- corner case 保持 linear 行为。
- benchmark 可以通过是否触发 fallback 来定位后续优化空间。

### 为什么 merge 只做固定 2 轮

merge 是 polish，不是核心搜索。固定小轮数可以限制 runtime，并避免退化成 graph agglomeration。当前 benchmark 中 2 轮足以消除大部分递归过切分带来的小簇问题。

### 为什么 routing cap 用 proxy 而不是 exact cap

cluster 间 total routing cap 方差是一个全局统计目标，如果在 draft split/merge 的每个候选上都调用 exact routing，会把 fast
算法重新拖回候选搜索型 runtime。当前实现用 root-to-sink Manhattan sum 表示 routing-cap proxy：它不能替代 final
electrical evaluation，但足够在局部搜索中判断哪一类 cluster 明显 cap-heavy / cap-light。final legality 仍由
`ConstraintEvaluator` 负责。

## 已知限制

1. 初始 longest-axis split 仍可能产生轴向边界 artifact；boundary polish 只能处理邻近且局部可改善的边界 sink。
2. merge/boundary neighbor 当前仍需要扫描 active drafts 来排序 nearest candidates，cluster 数很大时可进一步替换为 spatial
   bucket / k-nearest graph。
3. draft 阶段没有维护 pin-cap lower bound，因此 pin-cap 失败主要在 final evaluation/repair 中处理。
4. `max_fanout == 0` 时仍用 32 作为内部 packing fanout，可能产生比理论最少 cluster 更多的结果。
5. fallback 到 linear 会使个别失败 case runtime 接近 linear，但保证合法完整输出。

## 后续优化方向

1. 在 `ClusterDraft` 中增量维护 pin-cap lower bound，提前避免明显 cap-violating merge。
2. 用 spatial bucket 或 nearest-neighbor graph 替换 active draft 全扫描，降低 nearest candidate selection 的 `O(k^2 log k)` 成本。
3. 将 routing-cap proxy 从当前 star Manhattan sum 扩展为轻量 rectilinear Steiner / FLUTE lower-bound proxy。
4. 记录 fallback 触发次数和 repair split 次数到 benchmark CSV，便于跟踪 fast-only 覆盖率。
5. 将 `ConstraintEvaluator` 从 linear 目录语义上抽取为 shared clustering electrical evaluator，减少 fast 对 linear target 的概念依赖。
