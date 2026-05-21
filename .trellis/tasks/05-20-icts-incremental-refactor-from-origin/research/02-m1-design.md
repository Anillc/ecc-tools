# M1 · BoundSkewTree Pimpl 拆分设计

> Status: in-progress (2026-05-20)
> Scope: PRD §3 M1 only. M2/M3/M4 由其他 trellis-implement 子任务负责。

## 1. 起点 baseline (origin/cts_refactor)

```
src/operation/iCTS/source/module/routing/bound_skew_tree/tree/
├── BoundSkewTree.hh                373 lines (mega-class, 14 nested types + 17 data members)
├── BoundSkewTree.cc                9  `auto BoundSkewTree::` (ctors + cost utils)
├── BoundSkewTreeBalance.cc         21 `auto BoundSkewTree::` (chapter)
├── BoundSkewTreeEmbedding.cc       24 `auto BoundSkewTree::` (chapter)
├── BoundSkewTreeFlow.cc            8  `auto BoundSkewTree::` (chapter)
├── BoundSkewTreeInfeasibleMerge.cc 5  `auto BoundSkewTree::` (chapter)
├── BoundSkewTreeJoining.cc         16 `auto BoundSkewTree::` (chapter)
└── BoundSkewTreeTopology.cc        9  `auto BoundSkewTree::` (chapter)
```

## 2. 终点目标

```
src/operation/iCTS/source/module/routing/bound_skew_tree/tree/
├── BoundSkewTree.hh                ≤ 70 lines (slim facade with std::unique_ptr<detail::BoundSkewTreeImpl> _impl)
├── BoundSkewTree.cc                thin forwarders (2 ctors + dtor + 4 forwarders)
└── detail/
    ├── BoundSkewTreeImpl.hh        Pimpl aggregate (data + nested types + components + shared helpers)
    ├── BoundSkewTreeImpl.cc        ctor / dtor / shared method definitions
    ├── BstBottomUpTopDownDriver.{hh,cc}    (替代 BoundSkewTreeFlow.cc)
    ├── BstTopologyBuilder.{hh,cc}          (替代 BoundSkewTreeTopology.cc)
    ├── BstJoiningSolver.{hh,cc}            (替代 BoundSkewTreeJoining.cc)
    ├── BstBalanceSolver.{hh,cc}            (替代 BoundSkewTreeBalance.cc)
    ├── BstEmbeddingSolver.{hh,cc}          (替代 BoundSkewTreeEmbedding.cc)
    └── BstInfeasibleMergeSolver.{hh,cc}    (替代 BoundSkewTreeInfeasibleMerge.cc)
```

## 3. 组件类设计 (与 archived W3a 对齐, 适配 origin 命名)

### 3.1 `BoundSkewTreeImpl` (Pimpl 内核)

- Namespace: `icts::bst::detail`
- 内含 (lifted verbatim from BoundSkewTree.hh):
  - 14 nested types: `KMeansConfig` / `MergeAreas` / `EmbeddingStep` / `BalanceRefAxis` /
    `JoiningSegmentDelayQuery` / `SideDelay` / `BalancePointQuery` / `BalancePointResult` /
    `MergeDistances` / `MergeRegionSpan` / `SideState<T>` / `EndState<T>` / `TimingState<T>` /
    `AxisDelayFactor`
  - 17 data members: `_id`, `_owned_areas`, `_unmerged_nodes`, `_root_guide`, `_topology_mode`,
    `_root`, `_skew_bound`, `_rc_pattern`, `_unit_*_capacitance/resistance` x4,
    `_delay_quadratic_factor`, `_joining_region`, `_joining_segment`, `_merge_segment`,
    `_balance_points`, `_joining_corner`, `_feasible_merge_segment_points`
  - 6 `std::unique_ptr<BstXxxSolver>` (components owned by Impl)
  - 6 `friend class BstXxx;` declarations
- 公开方法 (供 6 个 component 跨组件互调):
  - 2 ctors, dtor, `run()` (forwards to driver)
  - Component accessors: `driver() / topologyBuilder() / joiningSolver() / balanceSolver() / embeddingSolver() / infeasibleMergeSolver()` (返回 `BstXxx&`)
  - Shared math from BoundSkewTree.cc (移到 Impl 保留):
    `getBestMatch` / `mergeCost` / `static distanceCost` / `makeArea` / `merge(Area*, Area*)`
    (2 参数版, 建 parent + 连指针) / `areaReset` / `static resetPointValues` /
    `static calcManhattanDistanceComponents`
  - Inline state accessors (lifted from BoundSkewTree.hh): `joiningRegionPoints / joiningSegmentPoints / mergeSegment / balancePoints / joiningCornerPoint / feasibleMergeSegmentPoints / joiningSegmentPoint / joiningRegionPoint`
  - Static array helpers: `otherSide / oppositeEnd / pointAt / linePoint`
  - 公开常量 `kHalfFactor`

### 3.2 `BstBottomUpTopDownDriver` (8 methods, 替代 BoundSkewTreeFlow.cc)

- 公开 entry points:
  - `run()` (called by `BoundSkewTreeImpl::run()`)
  - `merge(Area* parent, Area* left, Area* right)` (3-参数版 merge，编排 calcJoiningSegment + processJoiningSegment + constructMergeRegion；BstTopologyBuilder 也调它)
- private:
  - `bottomUp` / `bottomUpAllPairBased` / `bottomUpTopoBased` / `processBottomUpTopology`
  - `topDown` / `embedTree` / `updateEmbeddedNodeTiming`

### 3.3 `BstTopologyBuilder` (9 methods, 替代 BoundSkewTreeTopology.cc)

- 公开 entry points:
  - `biPartition()` / `biCluster()`
  - `static kMeansPlus(const std::vector<Area*>&, const KMeansConfig&)` (纯算法 helper, 暴露给 Impl 内 cross-check 或外部调用; 实际上仅 buildBiClusterTree 内部用)
- private:
  - `buildBiPartitionTree` / `buildBiClusterTree`
  - `static octagonDivide` / `static calcOctagon` / `static areaOnOctagonBound` / `static calcAreasCenter`

### 3.4 `BstJoiningSolver` (16 methods, 替代 BoundSkewTreeJoining.cc)

- 公开 entry points (driver / balance / infeasible 调用):
  - `calcJoiningSegment(const MergeAreas&)`
  - `processJoiningSegment(Area*)`
  - `constructMergeRegion(const MergeAreas&)` (orchestrates Balance + Infeasible + Embedding)
  - `calcJoiningRegionCorner(const Area&)` / `joiningRegionCornerExists(const size_t&) const`
  - `delayFromJoiningSegment(const JoiningSegmentDelayQuery&, const SideDelay&) const`
- private:
  - `initSide` / `calcJoiningSegment(Area*, Line&, Line&)` (2-overload private)
  - `calcJoiningSegmentDelay` / `updateJoiningSegment` / `addJoiningSegmentPoints`
  - `calcJoiningRegion` / `calcJoiningRegionEndpoints` / `calcNonManhattanJoiningRegionEndpoints`
  - `addTurnPoint` / `addFeasibleMergeSegmentToJoiningRegion`

### 3.5 `BstBalanceSolver` (21 methods, 替代 BoundSkewTreeBalance.cc)

- 公开 entry points (joining solver / infeasible 调用):
  - `calcBalancePoint(const Area&)` / `calcBalanceBetweenPoints(const BalancePointQuery&, BalancePointResult&) const`
  - `calcFeasibleMergeSegmentPoints(const Area&)`
  - `hasFeasibleMergeSegmentOnJoiningRegion() const`
  - `constructFeasibleMergeRegion(Area*) const`
- private:
  - balance point: `calcBalancePointOnLine` / `calcBalancePointOffLine` / `static calcMergeDist` / `static calcPointCoordOnLine` / `calcXBalancePosition` / `calcYBalancePosition`
  - feasible region: `calcFeasibleMergeSegmentOnLine` / `calcFeasibleMergeSegmentBetweenPoints` / `isJoiningRegionLine`
  - merge region span: `addMergeRegionBetweenJoiningSegments` / `addMergeRegionOnJoiningSegment` / `calcMergeRegionLeftIndex` / `calcMergeRegionSpan` / `appendMergeRegionPointsOnSegment` / `addMergeRegionPointFromJoiningRegion` / `calcSkewSlope`

### 3.6 `BstEmbeddingSolver` (24 methods, 替代 BoundSkewTreeEmbedding.cc)

- 公开 entry points:
  - `static embedChild(const EmbeddingStep&)` (driver 调用)
  - `static isTransformedRectArea / static isManhattanArea / static mergeRegionToTransformedRect`
  - `static calcAreaLineType(const Area&) -> LineType`
  - `static calcConvexHull(Area*)`
  - `static calcJoiningRegionArea(const Line&, const Line&) -> double`
  - `static locateBoundarySegment(Area*, Point&, Line&)`
  - `static pointSkew(const Point&) -> double`
  - `static checkPointDelay(Point&)`
  - 实例 (依赖 unit RC / pattern state):
    - `calcSimplePointDelays / calcSegmentPointDelays / calcPointDelays / updatePointDelaysByEndSide / calcIrregularPointDelays`
    - `pointDelayIncrease` (2 overloads) / `calcDelayIncrease`
    - `getJoiningRegionLine / getJoiningSegmentLine / setJoiningRegionLine / setJoiningSegmentLine`
    - `checkJoiningSegmentMergeSegment / checkUpdatedJoiningSegment`

### 3.7 `BstInfeasibleMergeSolver` (5 methods, 替代 BoundSkewTreeInfeasibleMerge.cc)

- 公开 entry points (joining solver 调用):
  - `constructInfeasibleMergeRegion(Area*) const`
  - `constructTransformedRectMergeRegion(Area*) const`
- private:
  - `calcMinSkewSection / calcDetourEdgeLength / refineMergeRegionDelay`

## 4. 跨组件依赖图 (摘自源码 grep)

```
BoundSkewTree (facade)
    └── BoundSkewTreeImpl::run() → BstBottomUpTopDownDriver::run()
                                          │
                            ┌─────────────┼──────────────┐
                            ↓             ↓              ↓
                   BstTopologyBuilder  (bottomUp)    (topDown)
                            │                              │
                            └→ Driver::merge(p,l,r)  ←→ BstEmbeddingSolver::embedChild
                                          │
                                          ↓
                            BstJoiningSolver::constructMergeRegion
                                          │
                            ┌─────────────┼─────────────┐
                            ↓             ↓             ↓
                   BstBalanceSolver  BstInfeasibleMergeSolver  BstEmbeddingSolver
```

跨组件调用：
- Driver → JoiningSolver (calcJoiningSegment / processJoiningSegment / constructMergeRegion)
- Driver → EmbeddingSolver (embedChild / pointDelayIncrease / pointSkew)
- Driver → Impl (getBestMatch / mergeCost / distanceCost / makeArea / merge(2) / areaReset)
- Driver → TopologyBuilder (biPartition / biCluster)
- TopologyBuilder → Driver (merge(3)) + Impl (makeArea)
- JoiningSolver → BalanceSolver (calcBalancePoint / calcFeasibleMergeSegmentPoints / hasFeasibleMergeSegmentOnJoiningRegion / constructFeasibleMergeRegion)
- JoiningSolver → InfeasibleMergeSolver (constructInfeasibleMergeRegion / constructTransformedRectMergeRegion)
- JoiningSolver → EmbeddingSolver (locateBoundarySegment / calcPointDelays / calcSegmentPointDelays / pointDelayIncrease / updatePointDelaysByEndSide / pointSkew / get*Line / set*Line / calcAreaLineType / calcConvexHull / calcJoiningRegionArea / checkJoiningSegmentMergeSegment)
- BalanceSolver → EmbeddingSolver (updatePointDelaysByEndSide / pointSkew / calcDelayIncrease / get*Line / linePoint helpers)
- BalanceSolver → JoiningSolver (joiningRegionCornerExists / joiningCornerPoint access)
- InfeasibleMergeSolver → BalanceSolver (calcBalanceBetweenPoints) + EmbeddingSolver (calcDelayIncrease / pointSkew) + Impl (calcManhattanDistanceComponents)

## 5. 命名/类型 映射 (origin vs archived HEAD)

| Archived HEAD | Origin (本任务遵循) |
|---|---|
| `BSTParameters` | `BSTRoutingConfig` |
| `TopoType` | `BSTRoutingTopologyMode` |
| `TopoType::kInputTopo` | `BSTRoutingTopologyMode::kSourceRouteTree` |
| `RCPattern` | `BSTRoutingRCPattern` |
| `set_pattern(...)` / `_pattern` | `set_rc_pattern(...)` / `_rc_pattern` |
| `parent->set_pattern(_pattern)` | `parent->set_rc_pattern(_rc_pattern)` |
| `algorithm/detail/` | `tree/detail/` |
| `BstArea.hh` 等 | `bound_skew_tree/component/Components.hh` (聚合) |
| `BstGeomCalc.hh` | `bound_skew_tree/geometry/GeomCalc.hh` |
| `adapter/BstParameters.hh` | `bound_skew_tree/config/BSTRoutingConfig.hh` |
| 错误日志 "Normal BST construction cannot use input-topology mode." | "Normal BST construction cannot use source-route-tree mode." |
| 错误日志 "BST input-topology root area is null." | "BST source-route-tree root area is null." |

## 6. 方法体重写规则 (bit-identical)

针对每个迁移的方法体，仅做以下 token-level 替换：

1. `_xxx` (member data) → `_impl._xxx`
2. `joiningRegionPoints(side)` 等 (state accessor) → `_impl.joiningRegionPoints(side)`
3. `pointAt(...)` / `linePoint(...)` / `otherSide(...)` / `oppositeEnd(...)` → `BoundSkewTreeImpl::pointAt(...)` 等 (static)
4. `pointSkew(...)` / `calcAreaLineType(...)` / `calcConvexHull(...)` / `calcJoiningRegionArea(...)` / `mergeRegionToTransformedRect(...)` / `embedChild(...)` / `isTransformedRectArea(...)` / `isManhattanArea(...)` / `locateBoundarySegment(...)` / `checkPointDelay(...)` → `BstEmbeddingSolver::pointSkew(...)` 等 (static methods on component)
5. `getJoiningRegionLine(side)` / `getJoiningSegmentLine(side)` / `setJoiningRegionLine(...)` / `setJoiningSegmentLine(...)` / `pointDelayIncrease(...)` / `calcDelayIncrease(...)` / `calcSimplePointDelays(...)` / `calcSegmentPointDelays(...)` / `calcPointDelays(...)` / `updatePointDelaysByEndSide(...)` / `calcIrregularPointDelays(...)` / `checkJoiningSegmentMergeSegment(...)` / `checkUpdatedJoiningSegment(...)` (instance, needs state) → `_impl.embeddingSolver().xxx(...)` (in non-Embedding components)
6. `calcBalancePoint(...)` / `calcBalanceBetweenPoints(...)` / `calcFeasibleMergeSegmentPoints(...)` / `hasFeasibleMergeSegmentOnJoiningRegion(...)` / `constructFeasibleMergeRegion(...)` (in non-Balance) → `_impl.balanceSolver().xxx(...)`
7. `constructInfeasibleMergeRegion(...)` / `constructTransformedRectMergeRegion(...)` (in non-Infeasible) → `_impl.infeasibleMergeSolver().xxx(...)`
8. `calcJoiningSegment(...)` / `processJoiningSegment(...)` / `constructMergeRegion(...)` / `joiningRegionCornerExists(...)` / `calcJoiningRegionCorner(...)` / `delayFromJoiningSegment(...)` (in non-Joining) → `_impl.joiningSolver().xxx(...)`
9. `merge(p, l, r)` 3-param (in TopologyBuilder via `merge(left, right)` lambda) → 通过 `_impl.merge(left, right)` (2-param, 仅创建 parent), then 上层 driver 再调 3-param. 实测原 Topology 仅用 2-param `merge`，所以无歧义.
10. `getBestMatch(...)` / `mergeCost(...)` / `distanceCost(...)` / `makeArea(...)` / `merge(left, right)` 2-param / `areaReset(...)` / `resetPointValues(...)` / `calcManhattanDistanceComponents(...)` → `_impl.xxx(...)`

## 7. 迁移顺序 (build PASS gate after each)

1. M1.a  创建 BoundSkewTreeImpl.{hh,cc} 含全部 nested types + data members + 共享方法 + 6 components 持有；保留 BoundSkewTree.hh / 7 chapter .cc 不变 → build (期望仍 PASS，因为没人 include Impl)
2. M1.b 一次性创建 6 个 component .hh/.cc, 把方法搬过去 (with token rewrite per §6)
3. M1.c 重写 BoundSkewTree.hh (≤ 70 lines, slim facade) + BoundSkewTree.cc (forwarders only)
4. M1.d 删除 7 chapter .cc + 修改 CMakeLists → build PASS

由于 BoundSkewTree.hh 公开 API 不变，外部 caller (BSTRouter / BSTRouterBinaryTopology) 无须修改。

## 8. CMakeLists 改动

`bound_skew_tree/CMakeLists.txt` source list 调整：
- 删除: 7 个 `tree/BoundSkewTree*.cc`
- 新增: `tree/BoundSkewTree.cc` (facade) + `tree/detail/BoundSkewTreeImpl.{cc}` + 6 × `tree/detail/Bst*.cc`

不新增子目录 add_library，遵循 origin 单 target `icts_source_module_routing_bst` 设计。

## 9. 终态文件计数 (期望)

| 项 | 期望值 |
|---|---:|
| `tree/BoundSkewTree.hh` 行数 | ≤ 70 |
| `tree/BoundSkewTree.cc` 行数 | ≤ 70 (薄 forwarders) |
| `tree/*.cc` (顶层) | 1 (仅 facade) |
| `tree/detail/*.{hh,cc}` | 14 (Impl + 6 components × 2) |
| `grep -c 'auto BoundSkewTree::' tree/detail/*.cc` | 0 |
| `grep -c 'auto BoundSkewTree::' tree/BoundSkewTree.cc` | ≤ 5 (forwarders, may use trailing return for forwarding) |

## 10. 实施结果

### 10.1 物理产出

| 文件 | 行数 | `auto BoundSkewTree::` 数 |
|---|---:|---:|
| `tree/BoundSkewTree.hh` | **61** | (header) |
| `tree/BoundSkewTree.cc` | **68** | 4 (run / get_root / set_root_guide / set_rc_pattern forwarders) |
| `tree/detail/BoundSkewTreeImpl.hh` | 296 | (header) |
| `tree/detail/BoundSkewTreeImpl.cc` | 271 | 0 (uses `BoundSkewTreeImpl::xxx`) |
| `tree/detail/BstBottomUpTopDownDriver.hh` | 60 | — |
| `tree/detail/BstBottomUpTopDownDriver.cc` | 270 | 0 |
| `tree/detail/BstTopologyBuilder.hh` | 64 | — |
| `tree/detail/BstTopologyBuilder.cc` | 391 | 0 |
| `tree/detail/BstJoiningSolver.hh` | 79 | — |
| `tree/detail/BstJoiningSolver.cc` | 580 | 0 |
| `tree/detail/BstBalanceSolver.hh` | 88 | — |
| `tree/detail/BstBalanceSolver.cc` | 645 | 0 |
| `tree/detail/BstEmbeddingSolver.hh` | 90 | — |
| `tree/detail/BstEmbeddingSolver.cc` | 478 | 0 |
| `tree/detail/BstInfeasibleMergeSolver.hh` | 53 | — |
| `tree/detail/BstInfeasibleMergeSolver.cc` | 137 | 0 |

### 10.2 验收 (PRD §3 M1)

| 项 | 期望 | 实测 | 状态 |
|---|---|---|---|
| `BoundSkewTree.hh` 行数 | ≤ 70 | 61 | PASS |
| `tree/` 顶层 | BoundSkewTree.{hh,cc} + detail/ | match | PASS |
| `tree/detail/` 文件数 | 14 (Impl×2 + 6 components×2) | 14 | PASS |
| `auto BoundSkewTree::` in detail/*.cc | 0 | 0 | PASS |
| `auto BoundSkewTree::` in tree/BoundSkewTree.cc | ≤ 5 (薄 forwarders) | 4 | PASS |
| `bash build.sh` | exit 0 | exit 0 (25/25 steps) | PASS |
| iEDA binary link | success | success | PASS |
| `SingletonRegistryTest` | 5 PASS | 5 PASS | PASS |
| BoundSkewTree 公开 API 签名 | 不变 | BSTRouter.cc + BSTRouterBinaryTopology.cc 无须修改 | PASS |
| 算法 bit-identical | 保留 | 仅 token rewrite (无逻辑改动) | PASS |
| origin 目录结构 | 保留 | tree/ 不改为 algorithm/，4 兄弟子目录不动 | PASS |

### 10.3 Failure log

| # | 描述 | 解决 |
|---|---|---|
| (无) | 一次构建即通过；无回滚 / 无修复迭代 | — |

### 10.4 关键设计决策记录

- **静态访问 vs 实例访问**：把"无需 instance state"的静态 helper 统一放在 `BoundSkewTreeImpl` 或对应 component 的 `static` 上（`pointAt` / `linePoint` / `otherSide` / `oppositeEnd` / `kHalfFactor` 在 Impl；`pointSkew` / `calcAreaLineType` / `calcConvexHull` / `calcJoiningRegionArea` / `embedChild` / `isTransformedRectArea` / `isManhattanArea` / `mergeRegionToTransformedRect` / `locateBoundarySegment` / `checkPointDelay` / `calcMergeDist` / `calcPointCoordOnLine` 在 component）。仅依赖 unit RC / pattern 等 per-instance state 的方法是 component 实例方法。
- **state 访问统一**：所有 component 通过 `_impl.xxx()` 访问 state accessors（`joiningRegionPoints` / `joiningSegmentPoints` / `mergeSegment` / `balancePoints` / `joiningCornerPoint` / `feasibleMergeSegmentPoints` / `joiningSegmentPoint` / `joiningRegionPoint`），通过 `_impl._xxx` 直接读 data members（friend 授权）。
- **cross-component 调度**：component 通过 `_impl.balanceSolver().xxx()` / `_impl.embeddingSolver().xxx()` / `_impl.joiningSolver().xxx()` / `_impl.infeasibleMergeSolver().xxx()` / `_impl.topologyBuilder().xxx()` / `_impl.driver().xxx()` 互调。
- **driver 持有 3-arg merge**：3-arg `merge(parent, left, right)` 是 driver 的成员（不是 Impl），因为它是 driver 编排的核心 step（调 joiningSolver 完成构造），TopologyBuilder 通过 `_impl.driver().merge(...)` 调用。但 TopologyBuilder 实际只用 `_impl.merge(left, right)` 的 2-arg 版（创建 parent + 连指针），不需要 3-arg；3-arg merge 仅在 driver 内自用。
- **保留 origin 选择**：`tree/` 命名不动；`BSTRoutingConfig` / `BSTRoutingTopologyMode` / `BSTRoutingRCPattern` 命名不动；`set_rc_pattern` / `_rc_pattern` 命名不动；clock_tree_conversion / component / config / geometry 兄弟子目录不动；BSTRouter.{hh,cc} 不动。
