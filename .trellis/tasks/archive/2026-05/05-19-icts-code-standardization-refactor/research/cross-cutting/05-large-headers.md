# Research: Large Headers Analysis (.hh > 300 lines)

- **Query**: 找出所有 .hh 文件中行数 >300 的，分析声明内容、是否应该拆分
- **Scope**: internal
- **Date**: 2026-05-19

扫描根：`src/operation/iCTS/source/`。

## 5.1 完整列表（>= 300 行）

| 行数 | 文件 | 主要内容 |
|---|---|---|
| 563 | `flow/synthesis/htree/segment_pruning/SegmentLibrary.hh` | 6+ 个 struct/class：`SegmentFrontierKind` enum, `SegmentFrontierKindSet`, `BufferPatternLibrary`, `SegmentFrontierRequest`, `SegmentCandidateFrontierSet`, `SegmentFrontierCatalog`, `RequiredLengthStateKey/Hash`, `SegmentClosureSolution`, `TopologyPatternNodeKind`, `TopologyPatternNode`, `TopologyPatternLibrary`, `PatternSearchResult`, `SegmentPatternLibraryCombiner`, `TopologyPatternLibraryCombiner` 等 |
| 382 | `database/adapter/fast_sta/FastStaTypes.hh` | 全部是 type aliases (`using FastStaXxxId = std::size_t`)、5 个 enum class、多个 struct（`FastStaPoint`, `FastStaPointKeyHash`, `FastStaLibertyAxis`, `FastStaLibertyTable`, etc.） |
| 371 | `module/routing/bound_skew_tree/BoundSkewTree.hh` | BST 的核心数据结构和接口 |
| 349 | `database/routing/SteinerTree.hh` | Steiner tree DB |
| 347 | `module/routing/bound_skew_tree/Components.hh` | BST 的几何 component（Region/Trapezoid/Line/…） |
| 323 | `utils/visualization/core/SvgCommon.hh` | SVG 常量 + Bounds + SvgTransform + palette + ComputeBounds/ComputeLoadCentroid/BuildAdjacency/BuildClusterColors 等多个 helper |
| 319 | `module/characterization/Frontier.hh` | 多模板类 `StateFrontierPruner`, `NullPruner`, key/hash structs |

## 5.2 个例分析

### SegmentLibrary.hh (563 行) — 最严重
- 包含 ≥14 个独立类型 / 类 / struct / enum：
  - 公共 API 类：`BufferPatternLibrary`, `TopologyPatternLibrary`, `SegmentFrontierCatalog`
  - 配对的 `*Combiner`：`SegmentPatternLibraryCombiner`, `TopologyPatternLibraryCombiner`
  - 私有结构：`SegmentFrontierKindSet`, `SegmentFrontierRequest`, `SegmentCandidateFrontierSet`, `RequiredLengthStateKey/Hash`, `SegmentClosureSolution`, `TopologyPatternNode`, `TopologyPatternNodeKind`, `PatternSearchResult`
- 多个类有 inline 实现（包括循环、状态机），导致 .hh 实际是 .cc。
- **应拆分**：
  - `SegmentFrontier.hh`（enum + KindSet + Request + CandidateFrontierSet + Catalog）
  - `BufferPatternLibrary.hh` + `TopologyPatternLibrary.hh`（两个独立库）
  - `*Combiner.hh`
  - 把 inline 大函数（`materialize`、`enrichBoundaryState` 等）下沉到 .cc

### FastStaTypes.hh (382 行) — 类型大杂烩
- 11 个 enum class（FastStaNodeKind / FastStaTransition / FastStaLibertyTableKind / FastStaLibertyAxisKind / FastStaDmpAlgorithm / FastStaSlewRole）
- 8 个 struct 和 hash 辅助
- 多个 `using FastStaXxxId = std::size_t` 和 `constexpr` invalid id
- **拆分方向**：
  - `FastStaIds.hh`（using + invalid 常量）
  - `FastStaEnums.hh`（5+ 个 enum）
  - `FastStaPoint.hh`（FastStaPoint + Hash）
  - `FastStaLibertyTypes.hh`（Liberty 相关 struct）

### BoundSkewTree.hh (371 行) + Components.hh (347 行)
- BST router 的核心 + 几何 component
- 这是 user PRD 里"互联网化用词"未点名的，但属于"大 .hh 散落到多个 .cc"的代表（BoundSkewTree.cc 还有 6 个伴生 .cc：Balance/Embedding/Flow/InfeasibleMerge/Joining/Topology）
- **拆分方向**：
  - Components.hh 拆为 `BstRegion.hh`、`BstTrapezoid.hh`、`BstLine.hh`
  - BoundSkewTree.hh 中的 enum / TopoType / 数据 struct → `BstTypes.hh`（已有 BSTTypes.hh，但还不够全）

### SteinerTree.hh (349 行)
- database/routing/ 的核心数据结构。是否真的需要 349 行的 header 值得审视；可能有 inline accessor 过度。

### SvgCommon.hh (323 行)
- 在 utils/visualization/core/
- 内含 30+ constexpr / 4 struct + 6 inline 函数
- 已经把 cluster 和 topology 都共享。
- **拆分方向**：
  - `SvgConstants.hh`（kCanvasMax / kPalette / kSvgColorXxx 等常量）
  - `SvgTransform.hh`（Bounds / SvgTransform / MapX/MapY / ComputeBounds / MakeTransform）
  - `SvgColorPolicy.hh`（BuildAdjacency / ChoosePaletteIndex / BuildClusterColors）

### Frontier.hh (319 行)
- module/characterization/，模板化的 frontier prunning 框架
- 多模板类 + 配套 key/hash struct
- **拆分**：把模板类按"功能层"拆，例如 `FrontierKey.hh` + `FrontierPruner.hh` + `NullPruner.hh`。

## 5.3 紧接 300 以下但偏大的（200–300 行）

| 行数 | 文件 |
|---|---|
| 291 | `database/timing/RCTree.hh` |
| 269 | `utils/logger/LogFormat.hh` |
| 267 | `utils/geometry/Geometry.hh` |
| 245 | `utils/graph/RootedTreeLCA.hh` |
| 231 | `module/characterization/CharBuilder.hh` |
| 230 | `flow/optimization/model/OptimizationTypes.hh` |
| 222 | `utils/logger/Schema.hh` |
| 221 | `flow/synthesis/htree/HTree.hh` |
| 207 | `database/config/Config.hh` |
| 206 | `module/characterization/HashJoinEngine.hh` |
| 206 | `flow/synthesis/htree/analytical_solver/AnalyticalSolverInternal.hh` |

这些虽然没超 300，但 utils/logger/LogFormat.hh 269 行全是 inline helper、utils/geometry/Geometry.hh 267 行全是模板函数 + `ProjectToL1Circle` 多份内嵌实现，已经"鼓"起来了。

## 5.4 是否暴露了过多 private 实现？

抽样阅读：

- **`utils/graph/RootedTreeLCA.hh` (245 行)** — 整个类的实现（构造、reset、lca、ancestorPath、calcDepth）全部在 header 内 inline。private 的 `calcDepth` 也暴露在 .hh 里。每次 include 都重新解析全部实现 → 编译时间影响明显。
- **`utils/logger/Schema.hh` (222 行)** — 类声明只有公共 API + private 状态字段（_stream / _path / _suspended_writers / _runtime_metrics）。实现都在 .cc。**模板**。
- **`utils/geometry/Geometry.hh`** — 全是模板函数，必须在 header；但 `ProjectToL1Circle` 不是模板，可以下沉。
- **`flow/synthesis/htree/segment_pruning/SegmentLibrary.hh`** — 多个类的逻辑全在 header 内 inline，包括包含 `LOG_FATAL_IF` 的复杂方法。这种"行为很重的 inline"会让任何 include 这个 header 的 .cc 都被强行带入复杂依赖。

## 5.5 综合建议（优先级）

| 优先级 | 文件 | 动作 |
|---|---|---|
| 高 | `SegmentLibrary.hh` (563) | 拆 3–5 个 hh，把大方法下沉到 .cc |
| 高 | `FastStaTypes.hh` (382) | 拆成 Ids / Enums / Point / LibertyTypes 4 个 |
| 高 | `Components.hh` (347) + `BoundSkewTree.hh` (371) | 一起规划，按 region/trapezoid/line 拆 |
| 中 | `SvgCommon.hh` (323) | 拆 Constants / Transform / Color 3 个 |
| 中 | `Frontier.hh` (319) | 模板类按职责拆 |
| 中 | `SteinerTree.hh` (349) | 评估 inline accessor 是否过多 |
| 低 | `RootedTreeLCA.hh` (245) | 把 `calcDepth` 下沉 |
| 低 | `Geometry.hh` (267) | `ProjectToL1Circle` 下沉到 .cc |

## Caveats / Not Found

- 没读完所有 200–300 行 header 的内容，仅看了首尾。
- 没分析"每个 .hh 被多少 .cc include" 来量化拆分对编译时间的具体收益。
- `FastStaTypes.hh` 实际有配套的 `FastStaTypes.cc`，里面可能已经把部分定义下沉，未详细读。
