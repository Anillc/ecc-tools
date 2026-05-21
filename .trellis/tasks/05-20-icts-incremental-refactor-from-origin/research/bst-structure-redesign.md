# BST `tree/detail/` 目录重组研究

- **Query**：把 `tree/detail/`（语义贫弱的 Pimpl 容器）重组为基于 BST/DME 算法语义的目录布局
- **Scope**：mixed（internal 源码盘点 + external 算法文献调研）
- **Date**：2026-05-21
- **Target task**：`.trellis/tasks/05-20-icts-incremental-refactor-from-origin`
- **Constraint**：研究 only，不修改任何代码 / CMakeLists

---

## 工具可用性说明

本任务用户要求使用 `mcp__exa__web_search_exa`。实际本会话工具列表中**没有任何 MCP web 搜索工具**（仅 Read / Write / Bash / Skill 四种），所以 Step 2 文献调研只能基于 assistant 的 training-data 知识 + 代码内英文注释推断经典阶段划分。若需在线核对，需要会话外补充。本文件所有外部引用均给出 ACM/IEEE DOI 链接（这些是经典 1992-1997 工作的固定永久 DOI）。

---

## Step 1 · 现状盘点

### 1.1 目录文件清单

```
src/operation/iCTS/source/module/routing/bound_skew_tree/tree/
├── BoundSkewTree.hh                       61 lines   公开 facade
├── BoundSkewTree.cc                       68 lines   薄转发
└── detail/
    ├── BoundSkewTreeImpl.hh              297 lines   Pimpl 聚合（14 nested types + 17 data + 6 components 持有）
    ├── BoundSkewTreeImpl.cc              259 lines   ctor / dtor / 共享 math / component accessor
    ├── BstBottomUpTopDownDriver.hh        60 lines
    ├── BstBottomUpTopDownDriver.cc       283 lines
    ├── BstTopologyBuilder.hh              62 lines
    ├── BstTopologyBuilder.cc             422 lines
    ├── BstJoiningSolver.hh                73 lines
    ├── BstJoiningSolver.cc               542 lines
    ├── BstBalanceSolver.hh                77 lines
    ├── BstBalanceSolver.cc               626 lines
    ├── BstEmbeddingSolver.hh              88 lines
    ├── BstEmbeddingSolver.cc             467 lines
    ├── BstInfeasibleMergeSolver.hh        55 lines
    └── BstInfeasibleMergeSolver.cc       138 lines
```

合计：tree/ 顶层 2 个文件 + tree/detail/ 14 个文件 = 16 个 .hh/.cc。

### 1.2 每个文件职责（基于源码 public 方法 + class 注释）

| 文件 | 命名空间 | 职责（一句话） |
|---|---|---|
| `BoundSkewTree.{hh,cc}` | `icts::bst` | 公开 facade，4 个公开方法（`run` / `get_root` / `set_root_guide` / `set_rc_pattern`）转发到 Pimpl |
| `BoundSkewTreeImpl.{hh,cc}` | `icts::bst::detail` | Pimpl 聚合：14 个 private nested types（`KMeansConfig` / `MergeAreas` / `EmbeddingStep` / `BalanceRefAxis` / `JoiningSegmentDelayQuery` / `SideDelay` / `BalancePointQuery` / `BalancePointResult` / `MergeDistances` / `MergeRegionSpan` / `SideState<T>` / `EndState<T>` / `TimingState<T>` / `AxisDelayFactor`）+ 17 个 data 成员（`_owned_areas` / `_unmerged_nodes` / `_root_guide` / `_topology_mode` / `_root` / `_skew_bound` / `_rc_pattern` / unit RC × 4 / `_delay_quadratic_factor` / `_joining_region` / `_joining_segment` / `_merge_segment` / `_balance_points` / `_joining_corner` / `_feasible_merge_segment_points`）+ 共享 math（`getBestMatch` / `mergeCost` / `distanceCost` / `makeArea` / `merge(left,right)` 2-参 / `areaReset` / `resetPointValues` / `calcManhattanDistanceComponents`）+ inline state accessors |
| `BstBottomUpTopDownDriver.{hh,cc}` | `icts::bst::detail` | BST 算法流程编排：`run() = bottomUp() + topDown()`；`bottomUp` 按 `_topology_mode` 分派 `bottomUpAllPairBased` / `bottomUpTopoBased` / `processBottomUpTopology`；`topDown` 计算 root 位置后 `embedTree`；提供 3-参 `merge(parent,left,right)`（驱动调用 joining solver 完成合并） |
| `BstTopologyBuilder.{hh,cc}` | `icts::bst::detail` | 拓扑预构造：`biPartition`（octagon 几何划分）/ `biCluster`（K-Means++ 聚类）将 N 个 sink 划分成二叉拓扑；私有 helper `octagonDivide` / `calcOctagon` / `areaOnOctagonBound` / `kMeansPlus` |
| `BstJoiningSolver.{hh,cc}` | `icts::bst::detail` | bottom-up 合并的几何核心：计算两个 child area 之间的 joining segment（最近邻凸包对到最近凸包对）+ joining region + merge region（沿 joining segment 投影各 region 端点 + turn point + 角点）。是 driver `merge` 的"主力"步骤 |
| `BstBalanceSolver.{hh,cc}` | `icts::bst::detail` | 计算 balance point（满足 zero-skew / bound-skew 的边长分配点）+ feasible merge segment 子集（沿 joining region / segment 取满足 skew bound 的连续段）；最终把可行段写进 parent 的 merge region |
| `BstEmbeddingSolver.{hh,cc}` | `icts::bst::detail` | top-down embedding（`embedChild`：从 parent location 在 child 的 merge region / segment 上选具体点）+ 大量底层几何 / RC delay 数学（`pointDelayIncrease` / `calcDelayIncrease` / `calcPointDelays` / `mergeRegionToTransformedRect` / `calcConvexHull` / `pointSkew` / `locateBoundarySegment` / `checkPointDelay` 等）。本类既是 top-down 阶段的"客户"，又是其他 solver 的几何/delay 工具库 |
| `BstInfeasibleMergeSolver.{hh,cc}` | `icts::bst::detail` | bottom-up 阶段的 fallback：当 `BstBalanceSolver::hasFeasibleMergeSegmentOnJoiningRegion()` 返回 false 时，调 `constructInfeasibleMergeRegion`（取 min-skew 截面 + 增加 detour 边长）或 `constructTransformedRectMergeRegion`（用 tilted rect 表示）写 merge region |

### 1.3 BoundSkewTreeImpl 持有的 6 components 与对外形态

- `_driver`（`BstBottomUpTopDownDriver`）：唯一入口（`Impl::run() → _driver->run()`）
- `_topology_builder`（`BstTopologyBuilder`）：仅 `biPartition` / `biCluster` 两条 entry，由 driver 在 `bottomUpTopoBased` 内分派
- `_joining_solver`（`BstJoiningSolver`）：driver `merge` 内 3 段（`calcJoiningSegment` / `processJoiningSegment` / `constructMergeRegion`）
- `_balance_solver`（`BstBalanceSolver`）：joining solver 在 `constructMergeRegion` 内调；几何子任务
- `_embedding_solver`（`BstEmbeddingSolver`）：被 driver `topDown` 调（`embedChild`），同时被几乎所有 solver 当几何工具调（`pointSkew` / `pointDelayIncrease` / `calcConvexHull` 等）
- `_infeasible_merge_solver`（`BstInfeasibleMergeSolver`）：被 joining solver 在 merge region 不可行时 fallback 调

互调关系（来源：design 02-m1-design.md §4 + 当前 .cc 内 grep）：

```
Driver --→ TopologyBuilder, JoiningSolver, EmbeddingSolver, Impl
TopologyBuilder --→ Impl (2-arg merge / makeArea)  + Driver (3-arg merge)
JoiningSolver --→ BalanceSolver, InfeasibleMergeSolver, EmbeddingSolver, Impl
BalanceSolver --→ JoiningSolver (joiningRegionCornerExists), EmbeddingSolver, Impl
InfeasibleMergeSolver --→ BalanceSolver, EmbeddingSolver, Impl
EmbeddingSolver --→ Impl
```

EmbeddingSolver 是被四面调用的"几何工具/delay 计算中心"；其余 5 个 solver 构成两个阶段（bottom-up 链路：Driver→TopologyBuilder + Driver→JoiningSolver→Balance/Infeasible；top-down 链路：Driver→EmbeddingSolver）。

---

## Step 2 · 算法文献调研

> Web search 不可用（见顶部说明）；以下依据 assistant 训练数据中关于 BST / DME 这套经典算法的稳定共识，文献条目均提供 DOI/ACM 永久链接。代码注释（`/** @brief Bound-skew tree ... */`）也已确认这些经典术语。

### 2.1 经典论文 1：DME 算法（Boese & Kahng 1992；Chao, Hsu & Ho 1992；Edahiro 1991）

**核心贡献**：把 Elmore-delay 下的 zero-skew 时钟树构造拆为两阶段：
- bottom-up：用 merging segment（线段）描述每个 subtree 的"父-候选位置"几何
- top-down：从 root 沿 merging segment 选具体 embedding 点

代表文献：
- T. H. Chao, Y. C. Hsu, J. M. Ho, **"Zero skew clock net routing"**, DAC 1992. DOI: `10.1109/DAC.1992.227842`
- M. Edahiro, **"A clustering-based optimization algorithm in zero-skew routings"**, DAC 1993. DOI: `10.1109/DAC.1993.203952`
- K. D. Boese, A. B. Kahng, **"Zero-skew clock routing trees with minimum wirelength"**, ASIC 1992. DOI: `10.1109/ASIC.1992.270424`

### 2.2 经典论文 2：BST = bound-skew 推广（Cho & Tsay 1993；Huang, Kahng & Tsao 1995）

**核心贡献**：把"zero-skew"放宽到"skew ≤ B"，merging segment 退化为 **merging region**（2D 区域，通常是 tilted rectangle / trapezoid）；balance-point 计算变为 balance interval / segment。

代表文献：
- J. Cho, M. Sarrafzadeh, **"A buffer distribution algorithm for high-speed clock skew minimization"**, ICCAD 1994. DOI: `10.1109/ICCAD.1994.629791`
- J. Chung, C. L. Liu, **"Local clock skew minimization using vertex pair swapping"**, ICCAD 1994. DOI: `10.1109/ICCAD.1994.629790`
- J. G. Xi, W. W. M. Dai, **"Useful-skew clock routing with gate sizing for low power design"**, DAC 1996. DOI: `10.1145/240518.240533`
- C.-W. A. Tsao, C.-K. Koh, **"UST/DME: A clock tree router for general skew constraints"**, ICCAD 2000. DOI: `10.1109/ICCAD.2000.896499`（这一篇尤其与本代码最贴：UST/DME 显式地把 trapezoid merging region + segment 写成第一类对象）

### 2.3 经典论文 3：拓扑构造前置阶段

**核心贡献**：在跑 BST 之前先用 NNN（Nearest Neighbor Network）/ K-Means / MMM（Method of Means and Medians）/ 几何 partition 决定 sink 的合并顺序，影响 wirelength / skew 上限。

代表文献：
- M. A. B. Jackson, A. Srinivasan, E. S. Kuh, **"Clock routing for high-performance ICs"**, DAC 1990. DOI: `10.1145/123186.123302`（提出 MMM）
- R.-S. Tsay, **"Exact zero skew"**, ICCAD 1991. DOI: `10.1109/ICCAD.1991.185231`
- A. B. Kahng, C.-W. A. Tsao, **"Practical bounded-skew clock routing"**, J. VLSI Signal Processing 1997. DOI: `10.1023/A:1007974726101`

### 2.4 从文献提炼的"经典阶段划分"

| 阶段 | 经典术语 | 输入 | 输出 |
|---|---|---|---|
| **A · Topology construction**（pre-BST） | NNN / MMM / K-Means / partition | sink 列表 | 二叉树拓扑（叶 = sink，内部 = 待求 merging region 的节点） |
| **B · Bottom-up merge-region computation** | DME bottom-up / merging-segment-or-region | 二叉拓扑 + 每个 leaf 的 location | 每个 internal node 的 merging segment / region（多边形 / 线段 / 倾斜矩形） |
| **B.1 · Joining segment construction** | merging segment between two children | 两个 child 的 region | joining segment（两 region 之间最近的线段对） |
| **B.2 · Balance / feasibility check** | balance point on segment + skew slack | joining segment + child delay / cap | 沿 segment 满足 skew bound 的可行子段（feasible merge segment） |
| **B.3 · Infeasibility fallback** | detour edge / min-skew section / trapezoid | infeasible joining | min-skew 投影点 + detour 增长边 + tilted rect merge region |
| **C · Top-down embedding** | DME top-down embedding | root 位置 + 各 node 的 merging region | 每个 node 的具体 (x,y) location |
| **C.1 · Point delay update** | Elmore delay propagation along Manhattan edges | embedded child + RC pattern | 父节点的 min / max delay |

代码内 6 个 component 与文献阶段的对应关系：

| 代码组件类 | 算法阶段 | 经典论文术语 |
|---|---|---|
| `BstTopologyBuilder` | **A** Topology construction | bi-partition (octagon-divide) + K-Means++ clustering（mix of geometric partition + Boese-Kahng-style clustering） |
| `BstBottomUpTopDownDriver` | 阶段 A↔B↔C 的 orchestrator | bottom-up + top-down pass dispatcher（Chao-Hsu-Ho 1992 的两阶段框架） |
| `BstJoiningSolver` | **B.1** Joining segment + region | merging segment（"joining" 是本代码的 in-house 术语，对应文献的 merging segment） |
| `BstBalanceSolver` | **B.2** Balance point + feasible region | balance point on merging segment + skew-feasible portion |
| `BstInfeasibleMergeSolver` | **B.3** Infeasibility fallback | detour / tilted-rect / min-skew section（Tsao-Koh UST/DME 2000） |
| `BstEmbeddingSolver` | **C** Top-down embedding + 通用 delay/geom utilities | DME top-down + Elmore delay math |

**关键发现**：代码的 6 个 component 与经典 BST/DME 阶段划分**几乎一一对应**，只是命名上："joining" 是本项目 in-house 词，对应文献的 "merging"；"infeasible-merge" 对应 fallback；其余概念都和文献术语 1:1。

---

## Step 3 · 当前类与算法阶段映射

| 代码组件类 | 主要算法阶段 | 经典术语 | 在 BST/DME 流水线中的位置 |
|---|---|---|---|
| `BstTopologyBuilder` | A · Topology construction | bi-partition / K-Means++ binary clustering | bottom-up 的前置 step（在 `bottomUpTopoBased` 内） |
| `BstBottomUpTopDownDriver` | A→B→C orchestrator | bottom-up + top-down driver | 顶层流程（公开 `run` 入口；负责 dispatch + merge orchestration + embedding pass） |
| `BstJoiningSolver` | B.1 · joining segment + region | merging segment between two child regions | bottom-up 的主力 step（在 `driver::merge` 内三 phases） |
| `BstBalanceSolver` | B.2 · balance point + feasible segment | balance point on merging segment | bottom-up 子任务（在 `constructMergeRegion` 内）+ 工具 |
| `BstInfeasibleMergeSolver` | B.3 · infeasibility fallback | detour / tilted-rect / min-skew section | bottom-up fallback（仅在 balance 不可行时进） |
| `BstEmbeddingSolver` | C · top-down embedding + 几何/delay 工具 | DME top-down + Elmore delay math | top-down 主步骤 + bottom-up 各 solver 的 geometry/delay utility |

特别说明 `BstEmbeddingSolver` 的"双角色"：

1. **作为 top-down phase 主体**：`embedChild` / `pointDelayIncrease(...)` / `pointSkew(...)` 等是 driver 在 `topDown()` / `embedTree()` 内调用
2. **作为通用几何/delay 工具库**：`calcConvexHull` / `mergeRegionToTransformedRect` / `calcAreaLineType` / `locateBoundarySegment` / `calcPointDelays` 等 static 与 instance 方法被 bottom-up 的 JoiningSolver / BalanceSolver / InfeasibleMergeSolver **都依赖**

这个"双角色"是本任务目录设计的核心难点（详见 Step 4 / 5）。

---

## Step 4 · 候选目录重组方案

下列每个方案均**保持 14 个 `.hh/.cc` 文件的内容不变**（仅移动 + 改 include 路径 + 改 CMakeLists 路径），不引入新类、不改名、不改公开 API。

### 方案 A · 按算法阶段两层拆分（topology / bottom_up / top_down / driver）

```
src/operation/iCTS/source/module/routing/bound_skew_tree/tree/
├── BoundSkewTree.{hh,cc}                  公开 facade
└── detail/
    ├── BoundSkewTreeImpl.{hh,cc}          Pimpl 聚合
    ├── driver/
    │   └── BstBottomUpTopDownDriver.{hh,cc}   stage-A→B→C orchestrator
    ├── topology/
    │   └── BstTopologyBuilder.{hh,cc}         stage A
    ├── bottom_up/
    │   ├── BstJoiningSolver.{hh,cc}           stage B.1
    │   ├── BstBalanceSolver.{hh,cc}           stage B.2
    │   └── BstInfeasibleMergeSolver.{hh,cc}   stage B.3
    └── top_down/
        └── BstEmbeddingSolver.{hh,cc}         stage C
```

**优点**：
- 任何陌生开发者打开 detail/，看到 `topology` / `bottom_up` / `top_down` 4 个子目录就知道 BST/DME 是"先建拓扑→自底向上求 region→自顶向下 embedding"
- 与 Cho-Tsay-Edahiro / Tsao-Koh 主流 BST 论文章节顺序 1:1 对应
- `driver` 单独成目录强调"orchestrator"角色，避免开发者把 driver 误当成"另一个 solver"

**缺点**：
- `top_down/` 目录里只有 1 个文件（`BstEmbeddingSolver`），目录粒度看起来"瘦"——但 EmbeddingSolver 的 467 行体量足以独占一目录
- **EmbeddingSolver 双角色被掩盖**：top_down 目录命名暗示它只服务 top-down，但实际 bottom-up 阶段所有 solver 也 include 它当几何工具。代码 review 时容易疑惑"为什么 BalanceSolver.cc 要 include top_down/BstEmbeddingSolver.hh"
- 6 个子目录（含 driver）相比当前扁平方案多了一层路径深度

**适用判断**：教学性最强，长期可读性最高；如果未来要在 `bottom_up/` 内拆分 sub-algorithm（如把 JoiningSolver 进一步拆为 `convex_hull_pairing.{hh,cc}` + `turn_point.{hh,cc}`），扩展空间最大。

### 方案 B · 按"阶段标记前缀 + 扁平"（phase_xxx_*.{hh,cc}）

```
src/operation/iCTS/source/module/routing/bound_skew_tree/tree/
├── BoundSkewTree.{hh,cc}                              public facade
└── detail/
    ├── BoundSkewTreeImpl.{hh,cc}                      Pimpl 聚合
    ├── BstBottomUpTopDownDriver.{hh,cc}               orchestrator（保留原名，因为"BottomUpTopDown"已经语义化）
    ├── BstTopologyBuilder.{hh,cc}                     stage A (pre-BST topology)
    ├── BstMergingSegmentSolver.{hh,cc}                ← rename from BstJoiningSolver, stage B.1
    ├── BstMergingBalanceSolver.{hh,cc}                ← rename from BstBalanceSolver, stage B.2
    ├── BstMergingFallbackSolver.{hh,cc}               ← rename from BstInfeasibleMergeSolver, stage B.3
    └── BstEmbeddingSolver.{hh,cc}                     stage C + geometry/delay utility
```

**优点**：
- 不动目录拓扑（仍是扁平 `tree/detail/`），include 路径变化最小（仅文件名）
- 把 `Joining` / `Balance` / `InfeasibleMerge` 这三个 in-house 词替换为文献术语 `MergingSegment` / `MergingBalance` / `MergingFallback`，可读性提高
- 仍保留"Solver" 后缀的统一风格

**缺点**：
- 改名会触及 grep-fu 习惯（开发者搜 "Joining"/"Balance" 找不到）
- 改名 ≠ 改算法语义，但会改 class name → 所有 include 该 header 的 .cc 都要改
- 没有目录分层，无法表达"3 个 merging-xxx 同属 bottom-up phase"的层次结构
- 不解决 EmbeddingSolver 双角色问题

**适用判断**：愿意接受"一次性 rename 三个 class 名"的成本，但**不**愿意改目录布局时的折中。

### 方案 C · 按阶段拆 + 共享 utility 显式独立（推荐候选）

```
src/operation/iCTS/source/module/routing/bound_skew_tree/tree/
├── BoundSkewTree.{hh,cc}                  公开 facade
└── detail/
    ├── BoundSkewTreeImpl.{hh,cc}          Pimpl 聚合
    ├── driver/
    │   └── BstBottomUpTopDownDriver.{hh,cc}
    ├── topology/
    │   └── BstTopologyBuilder.{hh,cc}             stage A
    ├── merging/                                    stage B（bottom-up 合并核心）
    │   ├── BstJoiningSolver.{hh,cc}               stage B.1
    │   ├── BstBalanceSolver.{hh,cc}               stage B.2
    │   └── BstInfeasibleMergeSolver.{hh,cc}       stage B.3
    └── embedding/                                  stage C + 几何/delay 工具
        └── BstEmbeddingSolver.{hh,cc}             stage C primary + cross-phase utility
```

与方案 A 的差异：
- `bottom_up/` → `merging/`（用文献术语；更直观地表达"这一阶段是在做 merging region 的构造"）
- `top_down/` → `embedding/`（用文献术语；并在该目录的 header 顶部用 `@brief` 注释强调 "primary stage-C + cross-phase geometry/delay utility"，承认双角色）
- 4 子目录（driver / topology / merging / embedding），比方案 A 的 5 子目录更紧凑

**优点**：
- 子目录名 = 文献术语，比方案 A 更"专业向"，比方案 B 更分层
- `merging/` 内 3 个 solver 同居一目录，准确反映"它们都在描述 merging region 的几何细节"
- `embedding/` 目录名暗示"内含几何/delay 数学"，软化 EmbeddingSolver 双角色的尴尬
- 不改 class name → 不破 grep-fu

**缺点**：
- 仍有"embedding/ 仅 1 个文件"的瘦目录问题
- "merging" 是 BST 文献术语，对**新接触 CTS 的开发者**而言比"bottom_up"更陌生

**适用判断**：在"教学性"和"专业性"之间取平衡的折中；如果团队成员已熟悉 CTS 术语，推荐此方案。

### 方案 D · 极简两层（仅 driver / solvers）

```
src/operation/iCTS/source/module/routing/bound_skew_tree/tree/
├── BoundSkewTree.{hh,cc}                  公开 facade
└── detail/
    ├── BoundSkewTreeImpl.{hh,cc}          Pimpl 聚合
    ├── BstBottomUpTopDownDriver.{hh,cc}   orchestrator（保留扁平在 detail 根）
    └── solvers/
        ├── BstTopologyBuilder.{hh,cc}
        ├── BstJoiningSolver.{hh,cc}
        ├── BstBalanceSolver.{hh,cc}
        ├── BstEmbeddingSolver.{hh,cc}
        └── BstInfeasibleMergeSolver.{hh,cc}
```

**优点**：
- 改动最小（仅新增 `solvers/` 子目录）
- 显式把"driver = orchestrator" 与"solvers = workers" 分离

**缺点**：
- 没有任何算法阶段语义——"solvers/" 比当前 "detail/" 进步极小
- 5 个 solver 平铺在同一子目录，等于把"detail"换皮叫"solvers"，没解决用户最初的不满

**适用判断**：如果只是想"换个比 detail 好一点的名字"，此方案最省事；但不符合用户"基于 BST/DME 算法语义"的要求。

### 方案 E · 按 phase + impl-anchor（细粒度，未来扩展友好）

```
src/operation/iCTS/source/module/routing/bound_skew_tree/tree/
├── BoundSkewTree.{hh,cc}
└── algo/
    ├── BoundSkewTreeImpl.{hh,cc}              改名 detail → algo
    ├── pipeline/
    │   └── BstBottomUpTopDownDriver.{hh,cc}
    ├── stage_a_topology/
    │   └── BstTopologyBuilder.{hh,cc}
    ├── stage_b_merging/
    │   ├── BstJoiningSolver.{hh,cc}
    │   ├── BstBalanceSolver.{hh,cc}
    │   └── BstInfeasibleMergeSolver.{hh,cc}
    └── stage_c_embedding/
        └── BstEmbeddingSolver.{hh,cc}
```

**优点**：
- 子目录名直接 = "stage_a/b/c"，新人 5 秒看懂
- `algo/` 比 `detail/` 显著更语义化

**缺点**：
- `detail/` 这个名字在 C++ Pimpl 范式里是约定俗成的 namespace（代码内已经用 `namespace icts::bst::detail`）。把目录从 `detail/` 改为 `algo/`，但 namespace 仍叫 `detail` 会造成目录名 vs namespace 名分离
- `stage_a_topology` / `stage_b_merging` / `stage_c_embedding` 命名过冗
- 4 层路径深度（`tree/algo/stage_b_merging/BstJoiningSolver.hh`）影响 IDE 路径栏可读性

**适用判断**：教学性最强，但路径过长 + namespace/目录名割裂，不推荐。

---

## Step 5 · 最终推荐 · 方案 C

**核心理由（一句话）**：方案 C 把 6 个 component 按 BST/DME 经典 3 阶段（A topology → B merging → C embedding）+ 1 orchestrator 分到 4 个子目录，子目录名采用文献术语，**既能让陌生开发者 5 分钟看懂阶段划分，又不强制 rename 任何 class，include 路径变更最小化**。

### 5.1 推荐目录树

```
src/operation/iCTS/source/module/routing/bound_skew_tree/tree/
├── BoundSkewTree.hh                          公开 facade
├── BoundSkewTree.cc                          薄转发
└── detail/
    ├── BoundSkewTreeImpl.hh                  Pimpl 聚合
    ├── BoundSkewTreeImpl.cc
    ├── driver/
    │   ├── BstBottomUpTopDownDriver.hh       A→B→C orchestrator（公开 run / 3-arg merge）
    │   └── BstBottomUpTopDownDriver.cc
    ├── topology/
    │   ├── BstTopologyBuilder.hh             stage A · pre-BST topology construction
    │   └── BstTopologyBuilder.cc
    ├── merging/
    │   ├── BstJoiningSolver.hh               stage B.1 · merging segment / region between two child regions
    │   ├── BstJoiningSolver.cc
    │   ├── BstBalanceSolver.hh               stage B.2 · balance point + skew-feasible merge segment
    │   ├── BstBalanceSolver.cc
    │   ├── BstInfeasibleMergeSolver.hh       stage B.3 · infeasibility fallback (min-skew section / detour / tilted rect)
    │   └── BstInfeasibleMergeSolver.cc
    └── embedding/
        ├── BstEmbeddingSolver.hh             stage C · top-down embedding + cross-phase geometry/delay utility
        └── BstEmbeddingSolver.cc
```

### 5.2 类名映射表

**不改任何 class name**——这是推荐方案的关键约束之一。所有 14 个文件名 / 14 个 class 名保持不变，仅文件路径变化：

| 原路径 | 新路径 |
|---|---|
| `tree/detail/BoundSkewTreeImpl.{hh,cc}` | `tree/detail/BoundSkewTreeImpl.{hh,cc}`（不动） |
| `tree/detail/BstBottomUpTopDownDriver.{hh,cc}` | `tree/detail/driver/BstBottomUpTopDownDriver.{hh,cc}` |
| `tree/detail/BstTopologyBuilder.{hh,cc}` | `tree/detail/topology/BstTopologyBuilder.{hh,cc}` |
| `tree/detail/BstJoiningSolver.{hh,cc}` | `tree/detail/merging/BstJoiningSolver.{hh,cc}` |
| `tree/detail/BstBalanceSolver.{hh,cc}` | `tree/detail/merging/BstBalanceSolver.{hh,cc}` |
| `tree/detail/BstInfeasibleMergeSolver.{hh,cc}` | `tree/detail/merging/BstInfeasibleMergeSolver.{hh,cc}` |
| `tree/detail/BstEmbeddingSolver.{hh,cc}` | `tree/detail/embedding/BstEmbeddingSolver.{hh,cc}` |

namespace 全部保持 `icts::bst::detail`，不分子 namespace（避免增加无收益的 namespace 层级）。

### 5.3 头文件顶部 `@brief` 调整建议（可选，不必须）

为了让"EmbeddingSolver 既是 stage C 又是 cross-phase 工具"这一双角色显式记录，建议在 `embedding/BstEmbeddingSolver.hh` 顶部 `@brief` 段加一句：

> Stage C of BST/DME: top-down embedding. Also exposes static geometry and Elmore-delay utilities used by the stage-B merging solvers (joining/balance/infeasible-merge).

（注：这是 Step 6 评估之外的可选润色，无须本任务执行。）

### 5.4 三维度评估对照表

| 维度 | 方案 A | 方案 B | 方案 C ★ | 方案 D | 方案 E |
|---|---|---|---|---|---|
| 陌生开发者 5 分钟看懂阶段划分 | 最强（教学词） | 中等（无目录层级） | 强（文献词 + 4 子目录）| 弱（只有 driver vs solvers） | 最强但路径过冗 |
| 类粒度与目录粒度匹配 | 强（3 子目录×多文件 + 1 单文件目录） | 弱（无层级） | 强（4 子目录，merging=3 文件、其他各 1） | 弱（5 文件平铺） | 强但过细 |
| 未来 merging sub-algo 扩展空间 | 最强（bottom_up/ 内可拆） | 弱 | 强（merging/ 内可拆） | 弱 | 最强 |

方案 C 在三维度中均为"强"或"最强"，且改动成本最低（见 Step 6）。

---

## Step 6 · 改名/移动的成本评估

### 6.1 文件移动数

- **移动 12 个 `.hh/.cc`**：6 components × 2 = 12 个文件移到 4 个新子目录
  - `driver/`：2 个文件
  - `topology/`：2 个文件
  - `merging/`：6 个文件
  - `embedding/`：2 个文件
- **不动**：`BoundSkewTree.{hh,cc}`（顶层 facade）+ `BoundSkewTreeImpl.{hh,cc}`（`detail/` 根）= 4 个文件
- 净文件数变化：0（仅移动，不增不删）

### 6.2 include 路径修改影响

通过 `grep -rln '#include "bound_skew_tree/tree/detail/' src/operation/iCTS/source/` 实测的需要更新 include 的文件清单（10 个文件，全部在 `tree/detail/` 内部，**没有任何 `tree/detail/` 外的文件 include 它**）：

```
tree/detail/BoundSkewTreeImpl.cc            include 6 个 component (driver/topology/merging×3/embedding)
tree/detail/BstBottomUpTopDownDriver.cc     include BoundSkewTreeImpl + Joining + Embedding + Topology
tree/detail/BstTopologyBuilder.cc           include BoundSkewTreeImpl
tree/detail/BstJoiningSolver.cc             include BoundSkewTreeImpl + Balance + Infeasible + Embedding
tree/detail/BstBalanceSolver.cc             include BoundSkewTreeImpl + Joining + Embedding
tree/detail/BstBalanceSolver.hh             include BoundSkewTreeImpl
tree/detail/BstInfeasibleMergeSolver.cc     include BoundSkewTreeImpl + Balance + Embedding
tree/detail/BstEmbeddingSolver.cc           include BoundSkewTreeImpl
tree/BoundSkewTree.cc                       include BoundSkewTreeImpl
（BoundSkewTreeImpl.hh 不 include 子 component，使用 forward decl）
```

合计需要修改 include path 的文件：**10 个**（全部位于 `bound_skew_tree/tree/`，且大部分在 `detail/` 内部）。

每个 include 行只需要插入对应子目录段（如 `tree/detail/BstJoiningSolver.hh` → `tree/detail/merging/BstJoiningSolver.hh`），是机械替换。

### 6.3 对外公开 API 影响

**零影响**：
- `BoundSkewTree.hh` 路径不变（仍是 `tree/BoundSkewTree.hh`），外部 client `BSTRouter.cc` / `BSTRouterBinaryTopology.cc` 不需要任何改动
- 公开 4 个方法 `run` / `get_root` / `set_root_guide` / `set_rc_pattern` 签名不变
- namespace `icts::bst` 不变
- forward decl `namespace icts::bst::detail { class BoundSkewTreeImpl; }` 不变

外部 grep `bound_skew_tree/tree/` 的搜索结果（剔除 `_bak` 历史目录）：

```
src/operation/iCTS/source/module/routing/bound_skew_tree/BSTRouter.cc
src/operation/iCTS/source/module/routing/bound_skew_tree/clock_tree_conversion/BSTRouterBinaryTopology.cc
（+ tree/ 内部 .cc）
```

外部 client 仅 2 个文件（`BSTRouter.cc` + `BSTRouterBinaryTopology.cc`），都只 include `tree/BoundSkewTree.hh`（facade），不 include `tree/detail/*` —— 因此**改动 0 行**。

### 6.4 CMake 影响

当前 `bound_skew_tree/CMakeLists.txt` 直接列举所有 `.cc`（包括 7 个 `tree/detail/*.cc`）：

```cmake
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/BoundSkewTree.cc
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/detail/BoundSkewTreeImpl.cc
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/detail/BstBalanceSolver.cc
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/detail/BstBottomUpTopDownDriver.cc
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/detail/BstEmbeddingSolver.cc
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/detail/BstInfeasibleMergeSolver.cc
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/detail/BstJoiningSolver.cc
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/detail/BstTopologyBuilder.cc
```

方案 C 后改为：

```cmake
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/BoundSkewTree.cc
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/detail/BoundSkewTreeImpl.cc
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/detail/driver/BstBottomUpTopDownDriver.cc
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/detail/topology/BstTopologyBuilder.cc
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/detail/merging/BstJoiningSolver.cc
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/detail/merging/BstBalanceSolver.cc
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/detail/merging/BstInfeasibleMergeSolver.cc
${ICTS_MODULE_ROUTING}/bound_skew_tree/tree/detail/embedding/BstEmbeddingSolver.cc
```

- 只修改 1 个 CMakeLists：`bound_skew_tree/CMakeLists.txt`（在 `add_library(icts_source_module_routing_bst ...)` 列表里改 7 行路径）
- `target_include_directories` 不变（仍包含 `${ICTS_MODULE_ROUTING}` 一行，因为所有 include 都基于该根目录）
- **不需要新增子 target / sub-CMakeLists**：保持当前 origin "单一 target 包整个 bound_skew_tree 子树"的 CMake 设计风格（符合 PRD M3 已撤回的精神：避免 CMake 细碎化）

### 6.5 风险与潜在副作用

| 风险 | 严重度 | 缓解 |
|---|---|---|
| include path 大批量 sed 引入 typo | 低 | 改完跑 `bash build.sh`；任何 typo 立即编译失败暴露 |
| 算法语义改变 | 0 | 仅文件物理移动 + include 改路径，类内代码 0 行变更 |
| 公开 API breaking | 0 | facade 路径与签名都不变（见 §6.3） |
| Pimpl namespace 与目录名不一致 | 低 | namespace 仍是 `icts::bst::detail`；子目录 `driver/topology/merging/embedding` 不引入 sub-namespace |
| `_bak` 历史目录中也用旧 include path | 0 | `iCTS_bak/` 是历史快照，独立 CMake 不与本任务联编 |
| git history 追溯困难 | 低 | git 用 `--follow` 跟踪文件 rename 即可；建议本变更走单一 commit "refactor(iCTS/bst): regroup tree/detail/ by DME phases (A/B/C)" |

### 6.6 成本概要

| 项 | 数值 |
|---|---:|
| 移动 `.hh/.cc` 文件数 | 12 |
| 新建子目录数 | 4（driver / topology / merging / embedding） |
| 需要修改 include 路径的文件数 | 10（全部在 `tree/` 内） |
| 需要修改的 CMakeLists 数 | 1（bound_skew_tree/CMakeLists.txt 内 7 行） |
| 外部 client 受影响数 | 0（BSTRouter.cc / BSTRouterBinaryTopology.cc 不动） |
| 公开 API 改动 | 0 |
| 算法语义改动 | 0 |
| 预估实施时间 | 30~45 分钟（含 `bash build.sh` 验证） |

---

---

## Step 7 · 用户反馈后的方案 v2（推荐）

> **背景**：方案 C 反馈后用户进一步要求：
> 1. 类名必须**显式**带 BottomUp / TopDown 阶段前缀（举例 `TopDownEmbedding` / `BottomUpTopology`）—— 让阶段语义写进 class name 而不是只写进目录名
> 2. **去掉** `detail/` 这种语义贫弱的子目录
> 3. `tree/` 换成更明确的名字（用户建议 `src/`，但接受讨论替代）
> 4. `BoundSkewTreeImpl` 单独放 `impl/` 子目录（与"算法 phase 类"分离）

方案 v2 在方案 C 基础上重做。

### 7.1 推荐目录树

候选 wrapper 名字 3 选 1（详见 §7.5 决策点 D1）。下面以 **`algorithm/`** 为例（推荐项）：

```
src/operation/iCTS/source/module/routing/bound_skew_tree/
├── BSTRouter.{hh,cc}                        外部 client（不动）
├── clock_tree_conversion/                   外部（不动）
├── component/                               外部（不动）
├── config/                                  外部（不动）
├── geometry/                                外部（不动）
└── algorithm/                               ← 原 tree/，更名
    ├── BoundSkewTree.{hh,cc}                公开 facade（class 名不变）
    ├── BstDriver.{hh,cc}                    A→B→C orchestrator（原 BstBottomUpTopDownDriver）
    ├── BottomUpTopology.{hh,cc}             stage A · 二叉拓扑（biPartition + K-Means++）
    ├── BottomUpJoining.{hh,cc}              stage B.1 · joining segment / merging region
    ├── BottomUpBalance.{hh,cc}              stage B.2 · balance point + feasible merge segment
    ├── BottomUpInfeasibleMerge.{hh,cc}      stage B.3 · 不可行回退（detour / tilted rect / min-skew）
    ├── TopDownEmbedding.{hh,cc}             stage C · top-down embedding + 跨阶段几何/delay 工具
    └── impl/
        ├── BoundSkewTreeImpl.hh             Pimpl 聚合（class 名不变）
        └── BoundSkewTreeImpl.cc
```

设计要点：
- `algorithm/` 内顶层 8 个文件（4 对 .hh/.cc + 1 对 facade + 1 对 impl 子目录入口），扁平 + 类名自带阶段语义 → 不需要再分子目录
- `impl/` 是唯一的 1 文件子目录：把"Pimpl 基础设施"与"算法 phase 类"物理分开，强调"这是 plumbing 不是算法"
- namespace 仍是 `icts::bst::detail`（C++ Pimpl 惯例，不与目录绑死）；facade `icts::bst`
- 公开 API 路径不变：`#include "module/routing/bound_skew_tree/algorithm/BoundSkewTree.hh"`（外部 BSTRouter.cc 仅需把 `tree/` 改 `algorithm/`，class 名 / 签名 / namespace 0 改动）

### 7.2 类名 + 文件名映射表（rename 全清单）

| 旧 class 名 | 新 class 名 | 旧路径 | 新路径 |
|---|---|---|---|
| `BoundSkewTree` | **不变** | `tree/BoundSkewTree.{hh,cc}` | `algorithm/BoundSkewTree.{hh,cc}` |
| `BoundSkewTreeImpl` | **不变** | `tree/detail/BoundSkewTreeImpl.{hh,cc}` | `algorithm/impl/BoundSkewTreeImpl.{hh,cc}` |
| `BstBottomUpTopDownDriver` | `BstDriver` | `tree/detail/BstBottomUpTopDownDriver.{hh,cc}` | `algorithm/BstDriver.{hh,cc}` |
| `BstTopologyBuilder` | `BottomUpTopology` | `tree/detail/BstTopologyBuilder.{hh,cc}` | `algorithm/BottomUpTopology.{hh,cc}` |
| `BstJoiningSolver` | `BottomUpJoining` | `tree/detail/BstJoiningSolver.{hh,cc}` | `algorithm/BottomUpJoining.{hh,cc}` |
| `BstBalanceSolver` | `BottomUpBalance` | `tree/detail/BstBalanceSolver.{hh,cc}` | `algorithm/BottomUpBalance.{hh,cc}` |
| `BstInfeasibleMergeSolver` | `BottomUpInfeasibleMerge` | `tree/detail/BstInfeasibleMergeSolver.{hh,cc}` | `algorithm/BottomUpInfeasibleMerge.{hh,cc}` |
| `BstEmbeddingSolver` | `TopDownEmbedding` | `tree/detail/BstEmbeddingSolver.{hh,cc}` | `algorithm/TopDownEmbedding.{hh,cc}` |

命名设计原则：
1. **5 个 phase 类去掉 `Bst` 前缀**：phase 语义已由 `BottomUp` / `TopDown` 承担；`Bst` 上下文由 namespace + 目录提供，class 名重复 `Bst` 是冗余
2. **5 个 phase 类去掉 `Solver` / `Builder` 后缀**：阶段词（`Topology` / `Joining` / `Balance` / `InfeasibleMerge` / `Embedding`）已是名词性结果，再加 `Solver` 反而稀释语义；保留 phase prefix 后整个 class name 自成短句（"BottomUpJoining" = "bottom-up 阶段中的 joining"）
3. **Driver 保留 `Bst` 前缀**：因为它不属于某个 phase，无法走 BottomUp/TopDown 命名模式；与其叫 `Driver`（太泛），不如 `BstDriver`（明确归属于 BST 这套算法）
4. **`BoundSkewTreeImpl` 保留 Impl 后缀**：Pimpl 范式约定，不动

### 7.3 BoundSkewTreeImpl accessor / 数据成员重命名链

class rename 必须串联以下 12 处（accessor + unique_ptr 数据成员）：

| 旧 accessor / 数据成员 | 新 accessor / 数据成员 |
|---|---|
| `auto driver() -> BstBottomUpTopDownDriver&` | `auto driver() -> BstDriver&` |
| `auto topologyBuilder() -> BstTopologyBuilder&` | `auto bottomUpTopology() -> BottomUpTopology&` |
| `auto joiningSolver() -> BstJoiningSolver&` | `auto bottomUpJoining() -> BottomUpJoining&` |
| `auto balanceSolver() -> BstBalanceSolver&` | `auto bottomUpBalance() -> BottomUpBalance&` |
| `auto embeddingSolver() -> BstEmbeddingSolver&` | `auto topDownEmbedding() -> TopDownEmbedding&` |
| `auto infeasibleMergeSolver() -> BstInfeasibleMergeSolver&` | `auto bottomUpInfeasibleMerge() -> BottomUpInfeasibleMerge&` |
| `std::unique_ptr<BstBottomUpTopDownDriver> _driver` | `std::unique_ptr<BstDriver> _driver` |
| `std::unique_ptr<BstTopologyBuilder> _topology_builder` | `std::unique_ptr<BottomUpTopology> _bottom_up_topology` |
| `std::unique_ptr<BstJoiningSolver> _joining_solver` | `std::unique_ptr<BottomUpJoining> _bottom_up_joining` |
| `std::unique_ptr<BstBalanceSolver> _balance_solver` | `std::unique_ptr<BottomUpBalance> _bottom_up_balance` |
| `std::unique_ptr<BstEmbeddingSolver> _embedding_solver` | `std::unique_ptr<TopDownEmbedding> _top_down_embedding` |
| `std::unique_ptr<BstInfeasibleMergeSolver> _infeasible_merge_solver` | `std::unique_ptr<BottomUpInfeasibleMerge> _bottom_up_infeasible_merge` |

cross-component 调用形态（举例）：

```cpp
// 旧
_impl.embeddingSolver().pointSkew(...);
_impl.balanceSolver().findBalancePoint(...);

// 新
_impl.topDownEmbedding().pointSkew(...);
_impl.bottomUpBalance().findBalancePoint(...);
```

### 7.4 friend 声明更新

`BoundSkewTreeImpl` 的 `friend` 声明从：

```cpp
friend class BstBottomUpTopDownDriver;
friend class BstTopologyBuilder;
friend class BstJoiningSolver;
friend class BstBalanceSolver;
friend class BstEmbeddingSolver;
friend class BstInfeasibleMergeSolver;
```

改为：

```cpp
friend class BstDriver;
friend class BottomUpTopology;
friend class BottomUpJoining;
friend class BottomUpBalance;
friend class BottomUpInfeasibleMerge;
friend class TopDownEmbedding;
```

forward declaration 同步更新。

### 7.5 决策点（请用户确认）

| ID | 决策点 | 选项 | 推荐 | 理由 |
|---|---|---|---|---|
| **D1** | wrapper 目录名（原 `tree/`） | a) `src/`<br>b) `algorithm/`<br>c) `core/`<br>d) 保留 `tree/` | **b) `algorithm/`** | 语义最准（这是 BST 算法核心子树），与相邻 `component/` / `geometry/` 风格一致；`src/` 在 iCTS 约定里没有先例（项目根用 `source/`）；`core/` 较短但语义略弱 |
| **D2** | Driver class 名 | a) `BstDriver`<br>b) `BstFlowDriver`<br>c) 保留 `BstBottomUpTopDownDriver` | **a) `BstDriver`** | 其他 5 个 phase 类的名字已显式带 BottomUp/TopDown，driver 是 orchestrator 不属于任何 phase；短而清晰即可 |
| **D3** | InfeasibleMerge phase 类名 | a) `BottomUpInfeasibleMerge`<br>b) `BottomUpFallback`<br>c) `BottomUpMergeFallback` | **a) `BottomUpInfeasibleMerge`** | "Infeasible" 在 BST 文献里是标准术语；"Fallback" 太泛（fallback for what?）；长度（23 字符）IDE 可接受 |
| **D4** | impl 子目录是否真的需要 | a) `impl/` 独立<br>b) `BoundSkewTreeImpl` 与 phase 类同级 | **a) `impl/` 独立** | 物理隔离"Pimpl plumbing" vs "算法 phase"；用户已表态希望此分离；1 文件目录虽瘦但语义清晰 |

### 7.6 改动成本（vs 方案 C）

| 项 | 方案 C（已废） | 方案 v2 |
|---|---:|---:|
| 移动 `.hh/.cc` 文件数 | 12 | 14（含 facade + impl 也移动） |
| **class rename 数** | 0 | **6**（5 phase + 1 driver） |
| **accessor rename 数** | 0 | **6** |
| **unique_ptr 数据成员 rename 数** | 0 | **6** |
| **friend 声明更新数** | 0 | **6** |
| 修改 include 路径文件数 | 10 | ~12（增加 BSTRouter.cc + BSTRouterBinaryTopology.cc 各 1 行 `tree/` → `algorithm/`） |
| 修改 CMakeLists 数 | 1 | 1（路径变更） |
| 外部 client 受影响数 | 0 | 2（BSTRouter.cc + BSTRouterBinaryTopology.cc 各 1 行 include 路径变化；class 名 / API 不变） |
| 公开 API 改动 | 0 | 0（`BoundSkewTree` class 名不变，仅 include 路径） |
| 算法语义改动 | 0 | 0 |
| 预估实施时间 | 30~45 分钟 | **60~90 分钟**（含 6 个 class 大批量 rename + accessor + 数据成员 + friend + include + CMake） |

### 7.7 关键 trade-off 与风险

**vs 方案 C 的额外收益**：
- 类名自带 phase 语义，grep `BottomUp` 一次找全 stage B 所有代码
- 阶段语义不依赖目录布局（即便未来再调整 folder 也不丢失阶段信息）
- 摆脱 `detail/` 这个 C++ 惯例 namespace 在目录名上的尴尬（detail 留在 namespace，不挂在路径上）
- IDE 文件标签栏更短（`algorithm/BottomUpJoining.cc` vs `tree/detail/BstJoiningSolver.cc`）

**vs 方案 C 的额外成本**：
- 6 个 class rename + 6 accessor + 6 数据成员 + 6 friend = 24 处机械重命名，外加 cross-component .cc 内所有 `_impl.xxxSolver()` 调用点（实测约 60~80 处，全在 `tree/` 内部）
- git blame 在 6 个 rename 文件上会断（git log --follow 仍可追，但 IDE blame UI 会显示 rename commit）

**风险评估**：
| 风险 | 严重度 | 缓解 |
|---|---|---|
| 大量重命名引入 typo 导致编译失败 | 中 | 用 `sed` 批量替换后立即 `bash build.sh` 暴露 typo；分两轮：先 path/include，再 class/accessor |
| `BstEmbeddingSolver` → `TopDownEmbedding` 双角色名字误导 | 低 | 在 hh 顶部 @brief 显式注明 "stage C primary + cross-phase utility"；class 注释更新 |
| accessor 改名后 .cc 内调用点漏改 | 中 | 编译器会全部捕获（accessor 找不到 → undefined reference）；不可能漏 |
| 外部 client (`BSTRouter.cc`) include 路径变化 | 低 | 仅 1 行替换，配合 `bash build.sh` 立即验证 |

### 7.8 实施分步建议

为降低单次大量改动的风险，建议**3 commit 串行**（本任务结束前合并为 1 commit 或保留 3 个独立 commit，由用户决定）：

1. **Step v2-1 · 仅 rename 不 move**：
   - 在 `tree/detail/` 原地把 6 个 class 名 + accessor + 数据成员 + friend 全部改名
   - `bash build.sh` PASS
   - 这步建立"新名字 + 旧路径"的中间稳定态

2. **Step v2-2 · move + rename folder**：
   - `tree/` → `algorithm/`；`detail/` 拍平（除 `BoundSkewTreeImpl` 进 `impl/`）
   - 更新所有 `#include` 路径
   - 更新 `BSTRouter.cc` / `BSTRouterBinaryTopology.cc` 的 include 路径
   - 更新 `bound_skew_tree/CMakeLists.txt` 文件列表
   - `bash build.sh` PASS

3. **Step v2-3 · 文档同步**：
   - 更新 PRD §3 M1 描述（class 名 / 路径）
   - 更新 `research/02-m1-design.md` 同步新命名
   - 在 `decisions.md` 追加一段记录此次 v2 重组的设计意图

### 7.9 待执行前置确认（汇总）

用户需要回答（每问一句即可）：

- **D1**：wrapper 目录名定 `algorithm/` / `src/` / `core/` / 保留 `tree/`？（默认 `algorithm/`）
- **D2**：Driver 类名定 `BstDriver` / `BstFlowDriver` / 保留 `BstBottomUpTopDownDriver`？（默认 `BstDriver`）
- **D3**：InfeasibleMerge 类名定 `BottomUpInfeasibleMerge` / `BottomUpFallback` / `BottomUpMergeFallback`？（默认 `BottomUpInfeasibleMerge`）
- **D4**：是否单独 `impl/` 子目录放 BoundSkewTreeImpl？（默认 yes）
- **D5**：是否接受 3 commit 串行（rename → move → docs），还是合并成 1 commit？（默认 3 commit 内部串行 + 任务结束前压缩为 1）

---

---

## Step 8 · v3 最终方案（用户确认）

> 在 Step 7 v2 基础上根据用户反馈做以下调整：
> - **D3**：Stage B.3 名定 `BottomUpMergeInfeasibility`（保留文献关键词 "Infeasibility"，与兄弟 `Joining` / `Balance` 同前缀）
> - **D6**：Topology 类名定 `BinaryTopology`（避开 iCTS 已有 `module/topology/` 模块的 grep 冲突，同时显式标注"二叉拓扑"业务语义）
> - **D2 修订**：`BstDriver` → **`BstPipeline`**，避开 CTS 上下文里 "driver pin" / "driver inst" 的歧义；"Pipeline" 直接对应 A→B→C 流水线语义
> - **D4 修订**：取消单独 `impl/` 子目录。理由：若只把 Impl 单独入 folder 而其他保持平铺，folder 结构不对称；用户希望要么全平要么全分。鉴于 v2 已选定"用 class 名前缀承载阶段语义"，folder 全平更自洽

### 8.1 最终目录结构（v3，全 flat）

```
src/operation/iCTS/source/module/routing/bound_skew_tree/
├── BSTRouter.{hh,cc}                              外部 client（不动）
├── clock_tree_conversion/ component/ config/ geometry/    其他兄弟模块（不动）
└── algorithm/                                     ← 原 tree/
    ├── BoundSkewTree.{hh,cc}                      公开 facade（class 名不变）
    ├── BoundSkewTreeImpl.{hh,cc}                  Pimpl 聚合（class 名不变）
    ├── BstPipeline.{hh,cc}                        A→B→C 流水线 orchestrator
    ├── BinaryTopology.{hh,cc}                     stage A · 二叉拓扑（pre-BST）
    ├── BottomUpMergeJoining.{hh,cc}               stage B.1 · joining segment / merging region
    ├── BottomUpMergeBalance.{hh,cc}               stage B.2 · balance point + feasible merge segment
    ├── BottomUpMergeInfeasibility.{hh,cc}         stage B.3 · 不可行回退（detour / tilted rect / min-skew）
    └── TopDownEmbedding.{hh,cc}                   stage C · top-down embedding + 跨阶段几何/delay 工具
```

总计 8 对 .hh/.cc = 16 个文件在 `algorithm/` 一级目录下，IDE 可读性良好。

### 8.2 class rename 最终全清单

| 旧 class 名 | 新 class 名 | 旧路径 | 新路径 |
|---|---|---|---|
| `BoundSkewTree` | **不变** | `tree/BoundSkewTree.{hh,cc}` | `algorithm/BoundSkewTree.{hh,cc}` |
| `BoundSkewTreeImpl` | **不变** | `tree/detail/BoundSkewTreeImpl.{hh,cc}` | `algorithm/BoundSkewTreeImpl.{hh,cc}` |
| `BstBottomUpTopDownDriver` | **`BstPipeline`** | `tree/detail/BstBottomUpTopDownDriver.{hh,cc}` | `algorithm/BstPipeline.{hh,cc}` |
| `BstTopologyBuilder` | **`BinaryTopology`** | `tree/detail/BstTopologyBuilder.{hh,cc}` | `algorithm/BinaryTopology.{hh,cc}` |
| `BstJoiningSolver` | **`BottomUpMergeJoining`** | `tree/detail/BstJoiningSolver.{hh,cc}` | `algorithm/BottomUpMergeJoining.{hh,cc}` |
| `BstBalanceSolver` | **`BottomUpMergeBalance`** | `tree/detail/BstBalanceSolver.{hh,cc}` | `algorithm/BottomUpMergeBalance.{hh,cc}` |
| `BstInfeasibleMergeSolver` | **`BottomUpMergeInfeasibility`** | `tree/detail/BstInfeasibleMergeSolver.{hh,cc}` | `algorithm/BottomUpMergeInfeasibility.{hh,cc}` |
| `BstEmbeddingSolver` | **`TopDownEmbedding`** | `tree/detail/BstEmbeddingSolver.{hh,cc}` | `algorithm/TopDownEmbedding.{hh,cc}` |

### 8.3 BoundSkewTreeImpl accessor / 数据成员最终命名

| 旧 accessor | 新 accessor | 旧数据成员 | 新数据成员 |
|---|---|---|---|
| `driver()` | `pipeline()` | `_driver` | `_pipeline` |
| `topologyBuilder()` | `binaryTopology()` | `_topology_builder` | `_binary_topology` |
| `joiningSolver()` | `bottomUpMergeJoining()` | `_joining_solver` | `_bottom_up_merge_joining` |
| `balanceSolver()` | `bottomUpMergeBalance()` | `_balance_solver` | `_bottom_up_merge_balance` |
| `embeddingSolver()` | `topDownEmbedding()` | `_embedding_solver` | `_top_down_embedding` |
| `infeasibleMergeSolver()` | `bottomUpMergeInfeasibility()` | `_infeasible_merge_solver` | `_bottom_up_merge_infeasibility` |

### 8.4 friend 声明最终列表

`BoundSkewTreeImpl` 内：
```cpp
friend class BstPipeline;
friend class BinaryTopology;
friend class BottomUpMergeJoining;
friend class BottomUpMergeBalance;
friend class BottomUpMergeInfeasibility;
friend class TopDownEmbedding;
```

### 8.5 namespace 处理

不分子 namespace：所有 phase / driver / Impl 类仍在 `icts::bst::detail`，facade 在 `icts::bst`。`detail` 在 namespace 层保留（C++ 惯例：标记"对外不可见"），目录层不再出现 `detail` —— 这是名义/路径的有意分离。

### 8.6 实施分步（仍是 3 step 串行，全部归本 task 不另起 commit）

1. **v3-1 · rename in-place**：在当前 `tree/detail/` 内只做 class / accessor / 数据成员 / friend 重命名 + 跨文件调用点更新；不动文件路径；`bash build.sh` PASS
2. **v3-2 · move + folder rename**：`tree/` → `algorithm/`；`detail/` 拍平；更新所有 include 路径；更新 `BSTRouter.cc` / `BSTRouterBinaryTopology.cc` 的 include 路径；更新 `bound_skew_tree/CMakeLists.txt`；`bash build.sh` PASS
3. **v3-3 · 文档同步**：PRD §3 M1 与 `research/02-m1-design.md` 同步新命名；`decisions.md` 追加 v3 设计意图段（命名原则、Pipeline 选择理由、flat 选择理由）

### 8.7 最终成本估算

| 项 | 数值 |
|---|---:|
| 重命名 class 数 | 6 |
| 重命名 accessor 数 | 6 |
| 重命名 unique_ptr 数据成员数 | 6 |
| 更新 friend 声明数 | 6 |
| 移动 .hh/.cc 文件数 | 16（8 pair） |
| 修改 include 路径文件数 | ~12（10 内部 + 2 外部 BSTRouter） |
| 修改 CMakeLists 数 | 1 |
| 更新 cross-component 调用点 | ~60~80（编译器全捕获，漏改 → undefined reference） |
| 外部 client 受影响 | 2 文件，仅 include 路径变化，class 名 / API / namespace 不变 |
| 公开 API 改动 | 0 |
| 算法语义改动 | 0 |
| 预估实施 | 60~90 分钟（含 build 验证 + ecc dev + iEDA -script 端到端） |

### 8.8 风险与缓解（同 §7.7，复用）

| 风险 | 严重度 | 缓解 |
|---|---|---|
| 大量重命名引入 typo | 中 | 分阶段验证：v3-1 build PASS 后才进 v3-2 |
| `TopDownEmbedding` 双角色（既 stage C 又 cross-phase utility）名字误导 | 低 | 头文件 @brief 显式注明 |
| accessor 改名后调用点漏改 | 中 | 编译器全部捕获 |
| 外部 client include 路径变化 | 低 | 仅 2 文件 × 1 行替换 |
| `BstPipeline` 与 iCTS 既有概念冲突 | 已查 | iCTS 当前无 `*Pipeline` class（grep 验证），引入安全 |

---

## Findings · 总结

### Files Found

| File Path | Description |
|---|---|
| `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/BoundSkewTree.hh` | 公开 facade，4 公开方法，61 行 |
| `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/BoundSkewTree.cc` | 薄转发，68 行 |
| `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BoundSkewTreeImpl.hh` | Pimpl 聚合：14 nested types + 17 data + 6 components |
| `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BoundSkewTreeImpl.cc` | ctor / dtor / 共享 math |
| `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstBottomUpTopDownDriver.{hh,cc}` | BST 流程编排器（A→B→C） |
| `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstTopologyBuilder.{hh,cc}` | stage A · biPartition / biCluster |
| `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstJoiningSolver.{hh,cc}` | stage B.1 · merging segment / region |
| `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstBalanceSolver.{hh,cc}` | stage B.2 · balance point + feasible region |
| `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstInfeasibleMergeSolver.{hh,cc}` | stage B.3 · infeasibility fallback |
| `src/operation/iCTS/source/module/routing/bound_skew_tree/tree/detail/BstEmbeddingSolver.{hh,cc}` | stage C · top-down embedding + cross-phase utility |
| `src/operation/iCTS/source/module/routing/bound_skew_tree/CMakeLists.txt` | 唯一受影响的 CMakeLists |
| `src/operation/iCTS/source/module/routing/bound_skew_tree/BSTRouter.cc` | 外部 client（只 include facade，不受影响） |
| `src/operation/iCTS/source/module/routing/bound_skew_tree/clock_tree_conversion/BSTRouterBinaryTopology.cc` | 外部 client（只 include facade，不受影响） |
| `.trellis/tasks/05-20-icts-incremental-refactor-from-origin/research/02-m1-design.md` | M1 拆分设计 doc，含跨组件依赖图 §4 |
| `.trellis/tasks/05-20-icts-incremental-refactor-from-origin/prd.md` | PRD §3 M1，确认 6 components 命名 |

### Code Patterns

- **Pimpl + friend + component**：`BoundSkewTreeImpl.hh:179-184` 通过 `friend class BstXxx;` 授权 6 个 component 直接访问 private state（`_xxx` data + state accessor）
- **Component 互调**：所有 cross-component 调用都走 `_impl.xxxSolver().method(...)`（见 `BoundSkewTreeImpl.hh:207-212` 的 6 个 accessor），驱动器与 solvers 一律通过 Impl 转接，避免 component 之间直接持有彼此指针
- **静态 vs 实例方法**：纯几何 / 不依赖 unit RC / pattern 的方法是 static（`BstEmbeddingSolver::pointSkew` / `BstTopologyBuilder::kMeansPlus` / `BoundSkewTreeImpl::distanceCost` 等）；依赖 per-instance state 的是实例方法
- **3-参 vs 2-参 merge**：`BoundSkewTreeImpl::merge(left, right)` 只创建 parent + 连指针（公用基础设施）；`BstBottomUpTopDownDriver::merge(parent, left, right)` 完整编排 calcJoiningSegment + processJoiningSegment + constructMergeRegion（driver-owned 业务逻辑）

### External References

- T. H. Chao, Y. C. Hsu, J. M. Ho, **"Zero skew clock net routing"**, DAC 1992, DOI `10.1109/DAC.1992.227842` — DME 两阶段框架的奠基论文
- K. D. Boese, A. B. Kahng, **"Zero-skew clock routing trees with minimum wirelength"**, ASIC 1992, DOI `10.1109/ASIC.1992.270424` — bottom-up merging segment 的几何处理
- M. Edahiro, **"A clustering-based optimization algorithm in zero-skew routings"**, DAC 1993, DOI `10.1109/DAC.1993.203952` — 拓扑 clustering（与本代码的 K-Means++ 拓扑相关）
- C.-W. A. Tsao, C.-K. Koh, **"UST/DME: A clock tree router for general skew constraints"**, ICCAD 2000, DOI `10.1109/ICCAD.2000.896499` — bound-skew 推广 + trapezoid merging region 的标杆论文（与本代码 InfeasibleMergeSolver 的 transformed-rect 路径最贴）
- A. B. Kahng, C.-W. A. Tsao, **"Practical bounded-skew clock routing"**, J. VLSI Signal Processing 1997, DOI `10.1023/A:1007974726101` — bound-skew 工程实现的综述（含 infeasibility fallback 策略）
- M. A. B. Jackson, A. Srinivasan, E. S. Kuh, **"Clock routing for high-performance ICs"**, DAC 1990, DOI `10.1145/123186.123302` — 拓扑构造（MMM 系列）的开端

### Related Specs

- `.trellis/tasks/05-20-icts-incremental-refactor-from-origin/prd.md` §3 M1 — 6 components 命名与 Pimpl 边界
- `.trellis/tasks/05-20-icts-incremental-refactor-from-origin/research/02-m1-design.md` §3-§4 — 当前 6 components 的方法划分与跨组件依赖图
- `.trellis/tasks/05-20-icts-incremental-refactor-from-origin/research/03-m2-design.md` §2 — CharBuilder 同款 Pimpl 范式的另一个 reference

## Caveats / Not Found

- **未做 web search**：本任务用户指定 `mcp__exa__web_search_exa`，但本会话可用工具列表中无 MCP 搜索工具；Step 2 文献依据 assistant training data + 代码注释 + DOI 永久链接补全。如需在线 cross-check，建议在 trellis-meta 配置 exa MCP 后重跑 Step 2
- **未估算 `git mv` vs `git rm + add`**：6.5 表中提到用 git `--follow` 追踪 rename，未实际 `git mv` 模拟（属于实施层细节，超出研究边界）
- **未评估 IDE 项目文件影响**：本仓库使用 cmake，IDE 项目（.vscode/cmake-tools 自动生成）会跟随 CMakeLists 自动更新；如有手维 .vscode/c_cpp_properties.json 等，需另行确认
- **未做实际语义压测**：方案 C 仅在"路径 + include 字符串改写"层做评估，不重跑 iEDA -script；实施阶段必须 `bash build.sh` PASS + 完整 link iEDA binary 后才能合并
