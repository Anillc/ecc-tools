# iCTS 重构后全面质量审视（comprehensive issues）

- **Scope**: `src/operation/iCTS/source/`
- **Baseline**: PRD `.trellis/tasks/05-19-icts-code-standardization-refactor/prd.md` §6 (验收标准)
- **Date**: 2026-05-20
- **Method**: 直接 grep / find / 读源码 (没有跑 build/test)

---

## 0. 一句话结论

> **物理验收（§6.1）大部分通过、但 CMake target 数（102 > 35）与三大 mega-class 拆分（§6.2）未达标；用户提出的三个具体问题中，问题 1（htree 悬浮 .hh）和问题 3（SubFlowOutcome.hh）属于"门面只暴露一个 .hh"目标未落地，问题 2（*Detail* 命名）的 4 个文件均未承载 CTS 业务语义。**

---

## 1. 用户三大反馈逐条复核

### 用户反馈 1：htree/ 悬浮 .hh —— 确认存在

**事实证据**：`flow/synthesis/htree/` 顶层（不进入子目录）共 4 个 .hh：

| 文件 | 行数 | 实际承载内容 | 是否被 htree/ 之外消费 |
|---|---|---|---|
| `HTree.hh` | 53 | `class HTree`（门面）+ 7 个 `using BuildOptions = HTreeBuildOptions` 等兼容别名 | 是（synthesis 内部用） |
| `HTreeLevelTypes.hh` | 57 | `HTreeLogContext` / `HTreeInsertedInstLevel` / `HTreeInsertedNetLevel` 3 个 POD（H-tree 内部分级元数据） | 否 |
| `HTreeOptions.hh` | 77 | `HTreeBuildOptions`（22 字段构建参数）+ `HTreeLevelPlan`（14 字段每层规划记录） | 否 |
| `HTreeResult.hh` | 149 | `HTreeRootDriverCompensationReport`（25 字段）+ `HTreeBuildResult`（**70+ 字段**，含 inserted_insts / inserted_pins / inserted_nets / 26 个 analytical_* 诊断字段） | 否 |

**外部消费者验证**：
```
$ grep -rn 'HTreeOptions\|HTreeBuildOptions\|HTreeResult\|HTreeBuildResult\|HTreeLevelTypes\|HTreeLogContext' \
    src/operation/iCTS/source --include='*.cc' --include='*.hh' | grep -v 'htree/HTree'
flow/synthesis/htree/solution/AnalyticalSolution.{hh,cc}:  // 仅 htree 内部子目录使用
```

`htree/` 子树之外**零消费**，全部三个支撑文件都是 H-tree 内部 implementation detail。`HTree.hh` 里的 `using BuildOptions = HTreeBuildOptions` 等别名说明作者已经意识到"这些类型本来就是 HTree 的"。

**判定**：违反用户期望。门面 `HTree.hh` 客观上是唯一对外入口，但同目录的 3 个 .hh 暴露了 250+ 行 H-tree 私有结构体（含具体诊断字段、analytical_* 计数器、级数计划缓存）。

**整改建议（按优先级）**：
1. （首选）把 3 个 .hh 的内容合并进 `HTree.hh`，让所有 H-tree-private 类型作为 `class HTree` 的 nested type（或同 namespace 内的 file-local struct）暴露；删掉 `using BuildOptions = HTreeBuildOptions` 这一类只为过渡而存在的别名。这是用户原始诉求最直接的实现。
2. （次选）保留三个文件但移入 `htree/private/` 或 `htree/detail/` 子目录，配合 CMake `target_include_directories(... PRIVATE)`，让 htree/ 子树之外的 includer 物理上找不到。该方案不动对外 API 但仍然给 htree/ 顶层留下"只有 HTree.hh"。
3. （不要做）继续保留三个并列 .hh —— 该状态即用户当前提出的问题。

### 用户反馈 2：`*Detail` 命名空洞 —— 确认存在 4 个文件

**事实证据**：

```
$ find src/operation/iCTS/source -name '*Detail*'
flow/evaluation/qor/QorEvaluationDetail.hh
module/routing/bound_skew_tree/adapter/BstAdapterDetail.hh
flow/synthesis/htree/analytical_solver/AnalyticalSolverDetail.hh
flow/synthesis/htree/compensation/RootDriverCompensationDetail.hh
```

逐个剖析"它到底是什么":

#### 2.1 `flow/evaluation/qor/QorEvaluationDetail.hh` (71 行)

- **真正承载**：QoR 评估的 11 个流水线步骤（`ClearStatistics` / `ClearSummary` / `AppendPathDepthStats` / `ClassifyClockNet` / `AccumulateInstStatistics` / `InstallClockNetRcTreeAndMeasure` / `AppendClockNetStatistics` / `AppendClockTimings` / `AppendClockLatencySkew` / `EmitEvaluationSummary` / `EmitRootInputToLeafOutputProbeReport`）+ 1 enum `ClockNetRole` + 1 struct `ClockNetMeasurement`。
- **CTS 业务语义**：这是"clock-net role 分类 + RC tree 注册 + 时序/skew 汇报"的流水线分解。
- **应叫什么**：拆为 `QorEvaluationSteps.hh`（11 个 free function 声明） + `ClockNetMeasurement.hh`（POD 与 enum）。或者：把 enum/struct 内联到 `QorEvaluation.hh`，把 11 个 free function 全部 sink 到 `QorEvaluation.cc` 的匿名 namespace（它们本身没有跨 .cc 共享需求 —— 该目录只有 `QorEvaluation.cc` + `QorEvaluationDetail.hh` 一对）。
- **结论**：命名不达标，且**这个 .hh 在物理上是冗余的**——`flow/evaluation/qor/` 目录只有一个 .cc 文件，根本不存在"多 .cc 需要共享头"的客观理由。

#### 2.2 `module/routing/bound_skew_tree/adapter/BstAdapterDetail.hh` (44 行)

- **真正承载**：2 个 free function：`ExportBstClockTree`（BST root area → ClockSteinerTree 导出）和 `BuildBstFromInputTopology`（输入 topology → BST 结果，反向适配）。
- **CTS 业务语义**：BST router 适配层的两个独立翻译步骤。
- **应叫什么**：拆成 `BstClockTreeExport.hh` + `BstInputTopologyBuilder.hh`（精确对应已有的 `BstClockTreeExport.cc` / `BstInputTopologyBuilder.cc`），或者把这两个函数转为 `BstRouter` 的 `private static` 方法。
- **结论**：命名不达标。.hh 里已经明明白白说"impl in BstClockTreeExport.cc / BstInputTopologyBuilder.cc"，那就直接拆成两个 .hh 与各自 .cc 对齐。

#### 2.3 `flow/synthesis/htree/analytical_solver/AnalyticalSolverDetail.hh` (206 行)

- **真正承载**：H-tree analytical solver 的 BEAM search 基础设施 ——
  - 8 个 struct：`ScoredSegment`、`ScoredSegmentCacheKey` + `Hash`、`PartialAnalyticalCandidate`、`UnitModelRef`、`PatternSequenceKey` + `Hash`、`FunctionalComposeContext`、`ResolvedModelInputs`
  - 1 enum `DiagnosticPatternStage`
  - 30+ free function 声明（`MakeFailure` / `ValidateRequest` / `ScoreModelSet` / `BuildBeamCandidates` / `ShortlistSegmentsForLevel` / …）
- **CTS 业务语义**：BEAM search + functional pattern compose + scored segment caching 三件事，是 analytical-solver 的内部 pipeline。
- **应叫什么**：按"模块语义"拆为 3 个 .hh：
  - `AnalyticalScoredSegment.hh`（ScoredSegment + Cache + Score* 函数）
  - `AnalyticalBeamSearch.hh`（PartialAnalyticalCandidate + Build/Trim/Shortlist 函数）
  - `AnalyticalPatternCompose.hh`（FunctionalComposeContext + UnitModelRef + Materialize/Decompose 函数）
- **结论**：命名严重不达标。"Detail" 既不告诉用户这是 BEAM search 也不说这是 cache infrastructure；体量 206 行已经远超过"detail"应该有的体量。

#### 2.4 `flow/synthesis/htree/compensation/RootDriverCompensationDetail.hh` (117 行)

- **真正承载**：root-driver 补偿模块的**缓存与查询基础设施**：
  - 2 个常量 `kRootDriverCompensationMethod`、`kRootDriverCompensationLoadSource`
  - 5 个 struct：`RootClosureTerminal`、`RootClosureWireEstimate`、`RootDriverCompensationCacheKey` + `Hash`、`RootClosureLoadEstimate`、`RootClosureLoadSignature` + `Hash`
  - 1 主状态 struct `RootDriverCompensationState`（含两个 unordered_map 缓存）
  - 1 free function `QueryRootClosureLoadEstimate`
- **CTS 业务语义**：root-closure 负载估计的"特征签名 + 缓存键 + 状态机"。
- **应叫什么**：`RootClosureLoadCache.hh` 或拆为 `RootClosureLoadEstimate.hh` + `RootDriverCompensationState.hh`。
- **结论**：命名不达标。"Detail" 完全掩盖了"这是缓存基础设施"这一关键信息。

#### 2.5 顺带：其它"为拆解而拆解"的可疑后缀

| 后缀 | 命中文件 | 是否真有 CTS 业务语义 |
|---|---|---|
| `*Internal*` | 0 | — |
| `*Impl*` | 0 | — |
| `*Detail*` | 4（见上） | 0 / 4 有业务语义 |
| `*Common*` | `utils/visualization/core/SvgCommon.hh`、`module/topology/fast_clustering/detail/RefinerCommon.hh` | 都是"复用工具凑合放" |
| `*Shared*` | 0 | — |
| `*Helper*` | `module/routing/helper/PinLocationHelper.{hh,cc}` | "Helper" 后缀本身就在 PRD §G5 禁用词列表 |
| `*Utils*` / `*Misc*` | 0 | — |
| `*Options.hh` | `flow/optimization/options/OptimizationOptions.hh`、`flow/synthesis/htree/HTreeOptions.hh` | 后者是反馈 1 中"应内化" |
| `*Result.hh` | `flow/synthesis/htree/HTreeResult.hh`、`flow/synthesis/trace/topology_result/TopologyResult.hh` | 前者反馈 1，后者尚可 |
| `*Outcome.hh` | `flow/SubFlowOutcome.hh` | 反馈 3 |
| `*Snapshot*` | 0（清理干净） | — |
| `*Wrapper*` | 0（已重命名为 IdbBridge） | — |
| `*Engine*` | `module/timing/TimingEngine.hh`（class）、`module/characterization/HashJoinEngine.hh`（forwarding shim）、`database/adapter/sta/StaEngineAccess.{hh,cc}`（包装 iSTA 的外部类） | TimingEngine、HashJoinEngine 均违反 PRD §G5 |

### 用户反馈 3：`flow/SubFlowOutcome.hh` 不优雅 —— 确认且更糟

**事实证据**：

- 文件位置：`src/operation/iCTS/source/flow/SubFlowOutcome.hh` (43 行)
- 内容：3 值 enum `SubFlowOutcome { kFinished, kSkipped, kFailed }`
- 实际消费者（grep 全仓库）：
  ```
  flow/setup/Setup.hh:28    #include "SubFlowOutcome.hh"
  flow/setup/Setup.hh:41    static auto run() -> SubFlowOutcome;
  flow/setup/Setup.cc:117   auto Setup::run() -> SubFlowOutcome
  ```
  **只有 Setup 一个 sub-flow 用了**。

对比其它 5 个 sub-flow 当前的 `run()` 返回类型：
| sub-flow | run() 返回类型 |
|---|---|
| Setup | `SubFlowOutcome` |
| Synthesis | `SynthesisTraceSummary` |
| Optimization | `OptimizationResult` |
| Instantiation | （未读，但 `Flow.hh:78` 持有 `InstantiationResult`） |
| Evaluation | `EvaluationResult` |
| Report | `ReportResult` |

**判定**：
- 文件解决的问题：本来设计来当作"统一 sub-flow 返回值契约"，但**只完成 1/6**，剩下 5 个 sub-flow 各自定义 `XxxResult` struct，并未采用。
- 命名问题：`SubFlowOutcome` 是通用框架词；CTS 业务上不存在"sub-flow"这个术语，CTS 文档里只说 "stage" / "pass" / "phase"。
- 物理放置：单独占用 `flow/` 顶层的一个 .hh，与门面 `Flow.hh` 并列，是用户最直观感受到的"门面不止一个"。

**整改建议**：
- 方案 A（彻底贯彻"统一契约"）：让 Synthesis / Optimization / Evaluation / Report / Instantiation 全部把 `run()` 改成返回 `SubFlowOutcome`（业务输出走 out-param 或 side-state），enum 重命名为 `CTSStageOutcome` 或 `ClockTreePassOutcome`，并把 .hh 移到 `Flow.hh` 顶部或 `flow/StageOutcome.hh`。
- 方案 B（承认未统一）：删除 `SubFlowOutcome.hh`，把 Setup::run 也改成返回 `bool` 或 `SetupResult`，flow/ 顶层只剩 `Flow.{hh,cc}`。
- **不要保留现状**：当前是"为统一而设的契约只统一了一个客户" + "命名是通用框架词" 双重问题。

---

## 2. A. PRD §6.1 grep-able 验收逐项核对

| # | 验收项 | 命令 | 结果 | 达标？ | 差距 |
|---|---|---|---|---|---|
| A1 | `*Internal.hh/.cc` = 0 | `find ... -name '*Internal.hh' -o -name '*Internal.cc'` | 0 | PASS | — |
| A2 | `namespace.*_internal` = 0 | `grep -rn 'namespace.*_internal' ...` | 0 | PASS | 但被 `namespace detail` 取代：`icts::optimization::detail`(18) / `icts::fast_clustering::detail`(15) / `icts::fast_sta::dmp_ceff::detail`(7) / `icts::visualization::detail`(1) — 共 41 个文件位于 `detail` 命名空间 |
| A3 | `snapshot` ≤ 3 | `grep -rin 'snapshot' --include='*.hh' --include='*.cc'` | **1** | PASS | 剩余 1 处：`database/adapter/fast_sta/liberty/FastStaLiberty.hh:47 snapshotBufferCell`。建议改名为 `loadBufferCell` / `cacheBufferCell` / `extractBufferCell` |
| A4 | `Wrapper*` 文件 = 0 | `find ... -name 'Wrapper*'` | 0 | PASS | 已统一替换为 `IdbBridge*` |
| A5 | banned class names | `grep -rEn 'class .*(Helper\|Manager\|Handler\|Service\|Provider\|Factory)\b' ...` | 0 | PASS（按字面 grep） | 但 PRD §G5 还禁用 **Engine / Polish / Draft / Desc**：仍有 `class TimingEngine`（`module/timing/TimingEngine.hh:30`）、文件名 `HashJoinEngine.hh` / `StaEngineAccess.{hh,cc}` / `PinLocationHelper.{hh,cc}` |
| A6 | `Request` in .hh | `grep -rn 'Request' --include='*.hh'` | **6 处** | 部分 | `AnalyticalSolverDetail.hh:165 ValidateRequest`、`WirelengthGrid.hh:39 CollectRequestedLevelLengthsUm`、`CharacterizationLibrary.hh:54/68/71/74 RequestKey / makeRequestKey / _request_key`。`ValidateRequest` 应叫 `ValidateInput`（接口是 `AnalyticalSolverInput`）；`RequestKey` 是 cache key 的名字，可接受但可改为 `CharacterizationKey` 更贴 CTS 语义 |
| A7 | fast_sta 顶层只 `FastSta.hh` | `find .../fast_sta -maxdepth 1 -name '*.hh'` | 仅 `FastSta.hh` | PASS | 实际拆为 11 子目录，每个子目录有自己的 .hh + .cc（详见 §3） |
| A8 | OptimizationPreparation 不再直接调 `FastStaBuilder` | `grep 'FastStaBuilder' OptimizationPreparation.cc` | 0 | PASS | 改走 `FastSTA::injectNetRouteTree`（OptimizationPreparation.cc:273），FastSta.hh:94 注释明确"replaces direct FastStaBuilder::injectNetRouteTree call sites" |
| A9 | CMake target ≤ 35 | `grep -rEn 'add_library' ... \| wc -l` | **102** | **FAIL** | 差距 67 个 target。分布：flow=50、database=23、module=22、utils=6；INTERFACE 18 + 有源 51（仍远高于 35） |
| A10 | .hh > 300 行需 spec 豁免 | `find ... -name '*.hh' \| xargs wc -l \| awk '$1>300'` | 4 个 | 部分 | `BoundSkewTree.hh` 382（docstring 自注"intentionally exceeds"）；`SteinerTree.hh` 355；`SvgCommon.hh` 329（docstring 自注"intentionally exceeds"）；`ParetoFront.hh` 326（无注释豁免）。三个 mega 头都没有真正的 PRD spec 豁免记录 |

### 第二组 grep（在 PRD 之外补充的覆盖性检查）

```bash
$ grep -rEn '^namespace [^;]*\{' src/operation/iCTS/source --include='*.hh' --include='*.cc' \
    | awk -F: '{print $3}' | sort | uniq -c | sort -rn | head
   265 namespace icts {
   162 namespace icts                # 多行声明形式（namespace icts \n {）
   132 namespace {                   # 文件级匿名（OK）
    11 namespace detail {            # 子级 detail（需检查每一个）
     8 namespace htree {
     7 namespace optimization {
```

22 个不同的 `namespace icts::xxx[::yyy]` 子命名空间散落在源码中（最多的子项：`icts::htree`=38 文件、`icts::bst`=19、`icts::optimization::detail`=18、`icts::fast_clustering::detail`=15、`icts::topology`=12、`icts::htree::analytical_solver`=12 …），namespace 与目录层级不严格对齐（PRD G12 未达）。

---

## 3. B. 顶层暴露面（exposure surface）逐目录核对

> 仅列每个目录的"门面 .hh 外的悬浮 .hh"，并标注是否应内化。

### B1 `flow/` 顶层

| 文件 | 性质 | 应否对外 | 整改建议 |
|---|---|---|---|
| `Flow.hh / Flow.cc` | 门面（Flow 单例 + 7 入口） | 是 | 但 #include 链已经把 `evaluation/qor/QorEvaluation.hh`、`synthesis/htree/characterization/library/CharacterizationLibrary.hh`、`instantiation/Instantiation.hh`、`synthesis/trace/SynthesisTrace.hh` 4 个 sub-sub-flow 头直接拉进公开头（Flow.hh:29-34），对外暴露面过宽 |
| **`SubFlowOutcome.hh`** | **悬浮** | **否（只有 Setup 用）** | 见用户反馈 3 |

### B2 `flow/setup/`

仅 `Setup.{hh,cc}` —— 干净。

### B3 `flow/synthesis/`

仅 `Synthesis.{hh,cc}` 在顶层 —— 干净。

### B4 `flow/synthesis/htree/`

见用户反馈 1：HTree.hh + 3 个悬浮 .hh（HTreeLevelTypes / HTreeOptions / HTreeResult）。

### B5 `flow/synthesis/htree/*` 各子目录顶层

| 子目录 | 顶层 .hh | 评估 |
|---|---|---|
| `analytical_solver/` | `AnalyticalSolver.hh`(facade)、`AnalyticalCandidate.hh`、`AnalyticalSelection.hh`、`AnalyticalSolverDetail.hh`、`AnalyticalValidation.hh` | 5 个 .hh，应该把 Candidate/Selection/Validation 内化为 AnalyticalSolver 的私有结构，Detail 按 §2.3 拆出语义化的 .hh |
| `characterization/` | `Characterization.hh`(facade) | 干净；下设 `library/`、`wirelength/` 两个子目录 |
| `compensation/` | `RootDriverCompensation.hh`(facade)、`RootDriverCompensationDetail.hh` | Detail 按 §2.4 改名为 `RootClosureLoadCache.hh` 或拆 2 个 |
| `constraint/` | `Constraint.hh` | 干净 |
| `embedding/` | `Embedding.hh`(facade)、`BufferPortTable.hh`、`EmbeddingState.hh` | 2 个悬浮 .hh；建议把 BufferPortTable / EmbeddingState 移入 `embedding/detail/` 或合进 Embedding.hh |
| `plan/` | `Plan.hh`(facade?)、`DepthPlan.hh` | 命名不分主次；建议合并或明确 facade |
| `region/` | `SinkLoadRegion.hh`(facade) | 干净（且整个子目录只有 1 .cc，子目录可能过早抽出） |
| `segment_pruning/` | `SegmentPruning.hh`(facade)、`BufferPatternLibrary.hh`、`BufferStrength.hh`、`PatternLibraryCombiner.hh`、`SegmentFrontier.hh`、`SegmentLibrary.hh`、`TopologyPatternLibrary.hh` | **6 个悬浮 .hh**！多数是模板类或 pattern 数据结构；应统一移入 `segment_pruning/types/` 或私有化 |
| `solution/` | `Solution.hh`、`AnalyticalSolution.hh`、`SolutionReport.hh`、`SolutionSelection.hh`、`StageReport.hh` | **5 个并列 .hh，无明显门面**；命名上 Solution / SolutionReport / SolutionSelection / StageReport 难分主次；建议明确 `Solution.hh` 为门面，其它移入 `detail/` 或合并 |
| `topology_pruning/` | `TopologyPruning.hh` | 干净（子目录只有 1 .cc） |

### B6 `flow/optimization/` 与 `flow/optimization/*` 子目录

```
flow/optimization/{candidate,model,mutation,options,preparation,report,solver,state}/
```

每个子目录都干净（仅 1 个 facade .hh + 1 个 .cc 或 INTERFACE）。
**但 `model/` 子目录有 6 个 type .hh**（OptimizationActionTypes / OptimizationBaselineTypes / OptimizationBufferTypes / OptimizationProfileTypes / OptimizationTopologyTypes / OptimizationTypes）——可酌情合为 2-3 个。

### B7 `flow/instantiation/`、`evaluation/`、`report/`

均干净（仅 Instantiation / Evaluation / Report 一对 .hh/.cc 在顶层）。

### B8 `module/routing/`

顶层无 .hh —— 干净；每个 sub-router 一个目录：

| 目录 | 顶层 .hh | 评估 |
|---|---|---|
| `bound_skew_tree/` (无顶层 .hh，下分 adapter/algorithm/geometry) | — | 见 B9 |
| `concurrent_bst_salt/` | `CBSRouter.hh` | 干净 |
| `database/` | （INTERFACE only，无 .hh） | **空目录冗余** —— PRD P2 已点名 |
| `flute/` | `FLUTERouter.hh` | 干净 |
| `helper/` | `PinLocationHelper.hh`、`SaltPinBuilder.hh` | "Helper" 后缀违反 PRD §G5 |
| `local_legalization/` | `LocalLegalization.hh` | 干净 |
| `router/` | `Router.hh` | 干净 |
| `salt/` | `SALTRouter.hh` | 干净 |

### B9 `module/routing/bound_skew_tree/` 三个子目录顶层

| 子目录 | 顶层 .hh | 评估 |
|---|---|---|
| `adapter/` | `BstRouter.hh`(facade)、`BstParameters.hh`、`BstAdapterDetail.hh` | Parameters 可保留（参数 POD），Detail 见 §2.2 拆为 BstClockTreeExport.hh + BstInputTopologyBuilder.hh |
| `algorithm/` | `BoundSkewTree.hh`（mega-class 382 行） | 见 §6 |
| `geometry/` | `BstArea.hh`、`BstGeomCalc.hh`、`BstInterval.hh`、`BstMatch.hh`、`BstPoint.hh`、`BstTrr.hh` | **6 个并列几何 .hh**；可合并为 `BstGeometryTypes.hh`（5 数据 POD）+ `BstGeomCalc.hh`（静态计算类） |

### B10 `module/topology/` 与子目录顶层

| 子目录 | 顶层 .hh | 评估 |
|---|---|---|
| 顶层 | `TopologyGen.hh` | 干净 |
| `cluster_constraints/` | `ClusterConstraintEvaluator.hh`、`ClusterConstraintTypes.hh` | Types 可内联 |
| `clustering/` | `Clustering.hh` | 干净 |
| `config/` | `TopologyConfig.hh` | 干净 |
| `fast_clustering/` | `FastClustering.hh`（facade）；`detail/` 子目录内置 6 .hh + 8 .cc | facade 干净；detail/ 子目录里 `RefinerCommon.hh` / `ClusterTypes.hh` 命名一般（"Common"、"Types"） |
| `kmeans/` | `KMeans.hh` | 干净 |
| `mcf/` | `MinCostFlow.hh` | 干净 |

### B11 `module/characterization/`

```
$ ls module/characterization/*.hh
CharBuilder.hh
Frontier.hh                    # 319 行 模板大集合（PRD P10）
HashJoinConcat.hh              # 206 行 模板 + namespace detail
HashJoinEngine.hh              # 25 行 forwarding shim（仅 include HashJoinConcat.hh），但仍违反 G5 "Engine" 禁用词
HTreeTopologyCharTable.hh
HTreeTraits.hh
ParetoFront.hh                 # 326 行
PatternCombiner.hh
SegmentCharTable.hh
SegmentTraits.hh
```

**10 个顶层 .hh，无 facade**。`CharBuilder.hh` 是事实门面，但与 9 个底层模板/表/Pareto 工具混在一起。该目录是用户描述"门面不清"的另一个典型样本。`HashJoinEngine.hh` 是过渡 shim（违反 G5）。

### B12 `module/analytical_characterization/`

3 个顶层 .hh：`AnalyticalCharacterization.hh`(facade)、`AnalyticalFit.hh`、`AnalyticalModel.hh`。两个支持头都被 htree 内部直接 include（`AnalyticalSolverDetail.hh:43`）。

### B13 `module/timing/`

仅 `TimingEngine.hh` —— 顶层干净，但 `class TimingEngine` 违反 G5。

### B14 `database/adapter/fast_sta/` 顶层

仅 `FastSta.hh` —— **完美门面**。下设 11 子目录每个有自己的 .hh：
```
builder/, clock_tree/, dmp_ceff/, incremental/, liberty/, parasitics/, power/, report/, timing/, types/
```
- `types/` 内 **12 个 .hh** 文件（FastStaCharTypes / Context / Enums / Geometry / Ids / IncrementalTypes / LibertyTypes / NodeNet / ParasiticTypes / PowerTypes / StatusTypes / TimingTypes / TypesFwd）—— 颗粒度过细，可合并为 4-5 个有更明确语义的类型头
- `dmp_ceff/` 内 4 个 .hh（DmpLibertyLookup / DmpNumerics / DmpSolver / FastStaDmpCeff）—— 比之前的 `*Internal.hh` 健康，但仍可合并

### B15 `database/adapter/sta/`

2 个顶层 .hh：`STAAdapter.hh`(facade)、`StaEngineAccess.hh`。Access 头是与 iSTA 外部 namespace 的转换层，可接受。

### B16 `database/adapter/sdc/`

5 个顶层 .hh：`ClockTraceResolver.hh`(facade?)、`ClockTraceTypes.hh`、`SdcClockModel.hh`、`SdcClockParser.hh`、`SdcClockReader.hh`。
- 命名分两套（`ClockTrace*` vs `SdcClock*`）但实际属于同一 SDC 解析子系统
- 4 个 .cc 用 `namespace icts::clock_trace`，与 `ClockTraceResolver.cc` 用 `namespace icts` 不一致（同子目录混 2 个 namespace）

### B17 `database/io/`

仅 2 个 .hh：`IdbBridge.hh`(facade)、`IdbClockWriterTypes.hh`。其中 ClockWriter 拆出 3 个 .cc（IdbClockReader.cc、IdbClockWriterAccess.cc、IdbClockWriter.cc）合并到 `icts_source_database_io` 单 target —— 算干净。

---

## 4. C. 命名清查（PRD §G5 禁用词残留）

| 禁用词 | 命中位置 | 替代命名建议 |
|---|---|---|
| `Internal` | 0 文件 / 0 namespace（PASS） | — |
| `Snapshot` | 1 函数：`FastStaLiberty.hh:47 snapshotBufferCell` | `loadBufferCell` / `cacheBufferCell` / `extractBufferCellEntry` |
| `Support` | 0（source 内）；test/ 内 31 个 *Support* 文件（不在 source/ 范围） | 后续 child task 处理 |
| `Request` | 6 处（见 A6） | `ValidateRequest` → `ValidateInput`；`RequestKey` → `CharacterizationKey` |
| `Wrapper` | 0（已改 IdbBridge） | — |
| `Engine` | `class TimingEngine`（module/timing/TimingEngine.hh:30）；`HashJoinEngine.hh`（forwarding shim）；`StaEngineAccess.{hh,cc}`（包装 iSTA） | TimingEngine → `TimingQuery` / `TimingFacade` / `StaTimingAccess`；HashJoinEngine.hh 整体删除（已是 shim）；StaEngineAccess 可保留（指代 iSTA 的 TimingEngine） |
| `Helper` | `PinLocationHelper.{hh,cc}` | `PinLocationResolver` / `PinAnchor` |
| `Manager / Handler / Service / Provider / Factory` | 0（PASS） | — |
| `Polish / Draft / Desc` | 0（PASS） | — |
| `Detail` | 4 文件（见 §2.1-2.4） | 各自见上 |
| `Common` | `SvgCommon.hh`、`RefinerCommon.hh` | `SvgRenderingPrimitives.hh`；`ClusterNeighborGraph.hh` 或拆分 |
| `Outcome` | `SubFlowOutcome.hh`（见用户反馈 3） | `ClockTreePassOutcome` 或删除 |
| `Misc / Utils / Shared / Impl` | 0（PASS） | — |

---

## 5. D. CMake / Target 边界

### D1 总 target 数

```
$ grep -rEn 'add_library' src/operation/iCTS/source | wc -l
102
$ grep -rEn 'add_library\(.*INTERFACE' src/operation/iCTS/source | wc -l
18
$ grep -rEn 'add_library\([^I]' src/operation/iCTS/source | wc -l
51
```

102 vs PRD 验收上限 35 —— **差距 67，未达标**。

按一级目录归类：
| 一级目录 | add_library 数 | 备注 |
|---|---|---|
| flow | 50 | 与 PRD 调研时的 50 一致 —— 没有任何收敛 |
| database | 23 | — |
| module | 22 | — |
| utils | 6 | — |
| `source/` 顶层 INTERFACE | 1 | — |

### D2 0-1 .cc/target 的子 target（"为头共享而设的子 target"）

部分示例（不完整列举）：
- `flow/synthesis/htree/region/` — 1 .cc + 1 hh
- `flow/synthesis/htree/topology_pruning/` — 1 .cc + 1 hh
- `flow/synthesis/htree/segment_pruning/` — 1 .cc + 7 hh
- `flow/synthesis/topology/buffer/`、`flow/synthesis/topology/sink/`、`flow/synthesis/topology/trunk/` — 每个仅 1 .cc
- `flow/synthesis/trace/distance/`、`domain_status/`、`layout/`、`topology_result/` — 每个 1 .cc
- `flow/report/visualization/gds/layer/`、`gds/writer/`、`svg/` — 每个 1 .cc
- `flow/optimization/{candidate,mutation,options,preparation,report,solver,state}/` — 每个 1-2 .cc

把这些每子目录 1 .cc 的 target 合并到上一层 target，flow 子目录从 50 → 约 12-15。

### D3 INTERFACE-only 冗余

- `module/routing/database/CMakeLists.txt` — INTERFACE only，目录里没有 .hh / .cc（已被 PRD P2 点名）
- `module/topology/{kmeans, mcf}/CMakeLists.txt` — INTERFACE only（实际是 header-only template，合理）
- `database/spatial/, characterization/, qor/, routing/, timing/` — INTERFACE only（合理，都是 header-only POD）

---

## 6. E. 跨层 / 跨 sub-flow 违例

### E1 utils → database（PRD G7、P6）—— **仍存在**

**Public header 包含违例**：
```
utils/visualization/core/SvgCommon.hh:43  #include "design/Pin.hh"
utils/visualization/core/SvgCommon.hh:44  #include "spatial/Tree.hh"
```
这是 utils 命名空间的 PUBLIC 头直接拉 database，反向依赖未消除。

**.cc 包含**（次要）：
```
utils/visualization/topology/TopologySvgWriter.cc:33-35  #include "design/Pin.hh"/spatial/{Point,Tree}.hh
utils/visualization/cluster/ClusterSvgWriter.cc:36-37   #include "design/Pin.hh"/spatial/Point.hh
```

**CMake 反向 link**：`utils/geometry/CMakeLists.txt` 与 `utils/visualization/CMakeLists.txt` 仍 link `icts_source_database_*`（grep -l 'database\|design\|spatial' 返回这两文件）。

### E2 `module/characterization` 与 `adapter/fast_sta`（PRD P7）—— **部分修复**

- ✓ 公开头 `CharBuilder.hh:42` 现仅 include `adapter/fast_sta/types/FastStaIds.hh`（带注释明确"P7 decoupling"）
- ✗ `.cc` 仍直接调 FAST_STA_INST：
  ```
  CharBuilderCircuit.cc:36       #include "adapter/fast_sta/FastSta.hh"
  CharBuilderSlewSampling.cc:33  #include "adapter/fast_sta/FastSta.hh"
  ```
  这是 module → database/adapter 的紧耦合（fast_sta 物理上仍在 database/adapter/ 下）

### E3 `report/Report.cc → Evaluation::run`（PRD P12）—— **已改为查询契约**

```
Report.cc:34  #include "evaluation/Evaluation.hh"
Report.cc:50  return evaluation_ready && Evaluation::hasEvaluationResult(evaluation_state);
Report.cc:62  const bool reused_evaluation_state = evaluation_ready && Evaluation::hasEvaluationResult(...)
```
不再反向调 `Evaluation::run`，注释（Report.cc:46-49）明确"This replaces the old 'Report calls Evaluation::run as fallback' reverse coupling"。
**结论**：从"反向调用"降级为"只读查询"，但 Report 仍直接 link 与 include Evaluation —— 仍是跨 sub-flow 耦合（用户视角的 unidirectional include 也算违例）。

### E4 `synthesis → instantiation`（PRD P12）—— **完全未修复**

```
flow/synthesis/distribution/ClockDistribution.cc:33  #include "instantiation/design_conversion/DesignConversion.hh"
flow/synthesis/distribution/ClockDistribution.cc:43/45/54/70/80   DesignConversion::addRootBufferForSinkDomain / partitionClockSinks / makeSinkDomainPrefix / connectSinkDomainDownstreamNet (5+ calls)
flow/synthesis/distribution/CMakeLists.txt:12  icts_source_flow_instantiation_design_conversion  ← PRIVATE-link

flow/synthesis/topology/Topology.cc:42  #include "instantiation/design_conversion/DesignConversion.hh"
flow/synthesis/topology/Topology.cc:192/194/264/315/364  DesignConversion::commitInsertedObjects / reconnectNet / makeSinkDomainPrefix / restoreClockSourceNetToClockLoads (5+ calls)
```

**判定**：synthesis 子树直接拉 instantiation/design_conversion，PRD P12 列为高优先级问题，本次重构未触及。

### E5 `Flow.hh` 公开依赖 sub-sub-flow 类型（PRD P12 延伸）

```
flow/Flow.hh:29-34  #include "ResettableInterface.hh"
                    #include "design/ClockLayout.hh"
                    #include "evaluation/qor/QorEvaluation.hh"
                    #include "instantiation/Instantiation.hh"
                    #include "synthesis/htree/characterization/library/CharacterizationLibrary.hh"
                    #include "synthesis/trace/SynthesisTrace.hh"
```
门面 `Flow.hh` include 了 5 个 sub-sub-flow 内部头（最深 4 层 `synthesis/htree/characterization/library/`）来当数据成员类型 —— 公开 API 表面被深度内部类型污染。

---

## 7. F. mega-class 拆分质量（PRD §6.2）

### F1 `BoundSkewTree`（routing/bound_skew_tree/algorithm/）—— **未达标**

**Header 状态**：`BoundSkewTree.hh` **382 行**（超 PRD 上限 300 行 82 行），docstring 19-29 行**显式承认**：

> @note This header intentionally exceeds the 300-line guideline: BoundSkewTree is the BST/DME algorithm mega-class whose private nested types and member functions are needed by the sibling `Bst*Solver.cc` translation units. Splitting the private members across multiple headers would reintroduce the `*Internal.hh` forbidden-vocabulary pattern; a real Pimpl boundary is reserved for a future refactor when the class is decomposed into cooperating solver components.

**实现状态**：6 个 chapter-name .cc 切片：
| .cc 文件 | 内含 | 行数 |
|---|---|---|
| `BoundSkewTree.cc` | ctor/dtor + 主入口 | ~ |
| `BstBalanceSolver.cc` | `BoundSkewTree::calcBalancePoint*` / `calcMergeDist` / `calcXBalancePosition` … 20+ 方法实现 | 582 |
| `BstDriver.cc` | BoundSkewTree:: 实现 | — |
| `BstEmbeddingSolver.cc` | `BoundSkewTree::embedChild` / `isTransformedRectArea` / `calcAreaLineType` … 15+ 方法 | ~ |
| `BstInfeasibleMergeSolver.cc` | `BoundSkewTree::constructInfeasibleMergeRegion` / `calcDetourEdgeLength` … | ~ |
| `BstJoiningSolver.cc` | BoundSkewTree:: 实现 | 519 |
| `BstTopologySolver.cc` | BoundSkewTree:: 实现 | ~ |

文件名虽然带 `Solver` 后缀像是新类，但**实际全部是同一个 `BoundSkewTree` mega-class 的成员函数实现**，与 `CharBuilderXxx.cc` 完全是同一种"文件名章节切片"反模式。**PRD §6.2 要求"不再用文件名章节切片"未达标**。

### F2 `CharBuilder`（module/characterization/）—— **未达标**

- `CharBuilder.hh` 242 行，仍持有所有 private nested 类型（`BuildProgress` 等）
- 11 个 chapter .cc 切片：`CharBuilder.cc` + `CharBuilderBuild.cc` + `CharBuilderCircuit.cc` + `CharBuilderConfig.cc` + `CharBuilderFeasibility.cc` + `CharBuilderPatternEnumeration.cc` + `CharBuilderPatternStorage.cc` + `CharBuilderSampleStorage.cc` + `CharBuilderSampling.cc` + `CharBuilderSlewSampling.cc` + `CharBuilderStaSampling.cc` + `CharBuilderTopology.cc`
- 验证：`grep CharBuilder:: CharBuilderBuild.cc` → 命中 `auto CharBuilder::build() -> void`
- 与 PRD 调研时 11 .cc 数完全一致 —— **零进展**

### F3 `FastClustering`（module/topology/fast_clustering/）—— **已达标**

- `FastClustering.hh` 已收敛为 **47 行 3-method 静态门面**（`buildElectricalBaseConfig` / `runDefault` / `run`）
- 实现下放到 `detail/` 子目录：`detail/ClusterFinalizer.{hh,cc}`、`ClusterGeometry.{hh,cc}`、`ClusterPartitioner.{hh,cc}`、`ClusterRefiner.{hh,cc}`、`ClusterTypes.hh`、`RefinerCommon.{hh,cc}`、`BoundaryCandidates.cc`、`BoundaryRefiner.cc`、`BoundarySearch.cc`、`MergeRefiner.cc`
- `namespace icts::fast_clustering::detail` 隔离
- **唯一遗憾**：`RefinerCommon.hh` / `ClusterTypes.hh` 命名一般，但实质是组件抽象（不是 chapter slicing）

### F4（PRD 未点名但同模式）`AnalyticalSolver`（flow/synthesis/htree/analytical_solver/）—— **新增 chapter slicing**

```
AnalyticalSolver.cc                  # 主入口
AnalyticalSolverCandidateBuild.cc    # chapter
AnalyticalSolverInput.cc             # chapter
AnalyticalSolverModel.cc             # chapter
AnalyticalSolverShortlist.cc         # chapter
AnalyticalSolverTrim.cc              # chapter
+ AnalyticalCandidate.{hh,cc}        # 类
+ AnalyticalSelection.{hh,cc}        # 类
+ AnalyticalValidation.{hh,cc}       # 类
+ AnalyticalSolverDetail.hh          # 206 行私有 types + 30+ free fn
```

5 个 `AnalyticalSolverXxx.cc` 是同一个流程的文件名章节切片，遵循着和 BoundSkewTree、CharBuilder 一样的反模式。

### F5 `STAAdapter`（database/adapter/sta/）—— chapter slicing 仍存在

```
STAAdapter.cc + STAAdapterCellQuery.cc + STAAdapterClockLookup.cc +
STAAdapterRcTree.cc + STAAdapterRootDriverQuery.cc + STAAdapterTimingUpdate.cc + STAAdapterWireRc.cc
```

6 chapter 切片（PRD §3-P5 表内已点名"STAAdapter 30+ 方法 / 7 .cc"，本次未拆为组件）。

---

## 8. G. 总评

### G1 量化总览

| 维度 | PRD 目标 | 当前 | 状态 |
|---|---|---|---|
| `*Internal.hh/cc` | 0 | 0 | ✓ |
| `_internal` namespace | 0 | 0（但 41 个 `detail` namespace 文件） | ✓（条文 PASS，精神部分迁移） |
| snapshot 调用 | ≤3 | 1 | ✓ |
| Wrapper* 文件 | 0 | 0（重命名 IdbBridge） | ✓ |
| Helper/Manager/Service/… class 名 | 0 | 0 | ✓ |
| Request in .hh | 0 | 6 | ✗ |
| fast_sta 顶层只 FastSta.hh | yes | yes | ✓ |
| OptimizationPreparation 不调 FastStaBuilder | yes | yes | ✓ |
| CMake target 数 | ≤35 | **102** | **✗（严重）** |
| .hh > 300 行 | 有 spec 豁免 | 4 个超长（仅 2 个有 docstring 自注） | ✗ |
| 6 sub-flow 统一范式 | yes | 6 个都有 init/run/report 三方法；但 run() 返回类型完全异构（SubFlowOutcome 只 1/6 用） | ✗（半成品） |
| fast_sta 拆 ≥10 子目录 | yes | 11 子目录（types/、builder/、liberty/、parasitics/、dmp_ceff/、clock_tree/、incremental/、timing/、power/、report/） | ✓ |
| BST / CharBuilder / FastClustering mega-class 不文件名章节切片 | yes | BST ✗、CharBuilder ✗、FastClustering ✓ | ✗（2/3） |
| utils 不 link database | yes | utils/geometry 与 utils/visualization 仍 link | ✗ |

### G2 距离 PRD §6 验收的剩余差距清单（按优先级排序）

#### P0（必须做才能算"完成"）

1. **CMake target 收敛 102 → ≤35**（PRD §6.1 A9）。当前完全未启动；flow 子目录 30+ 个 1-cc target 是首要合并对象。
2. **BoundSkewTree mega-class 拆分**（PRD §6.2 F1）。Header 自注承认"intentionally exceeds"，6 个 `Bst*Solver.cc` 是文件名章节切片伪装成 solver 类。需要真正引入 `BstBalanceSolver` / `BstEmbeddingSolver` / … 独立类（或 Pimpl）。
3. **CharBuilder mega-class 拆分**（PRD §6.2 F2）。11 chapter .cc 完全未拆，进展 0。
4. **用户反馈 1（HTree 悬浮 .hh）**。3 个非 HTree.hh 的 .hh 没有任何 htree 外消费者，应合并或私有化。
5. **用户反馈 3（SubFlowOutcome.hh）**。只用了 1/6，要么彻底统一要么删除。

#### P1（强烈建议同周期完成）

6. **用户反馈 2（4 个 *Detail*.hh 改名）**：QorEvaluationDetail → 拆 Steps + Types；BstAdapterDetail → 拆 2 个语义名 .hh；AnalyticalSolverDetail → 拆 3 个语义名 .hh；RootDriverCompensationDetail → 改名 RootClosureLoadCache。
7. **完成 sub-flow 范式统一**（PRD G1）。让 Synthesis/Optimization/Evaluation/Report/Instantiation 的 run() 全部返回同一 outcome 类型，或者明确放弃统一并删除 SubFlowOutcome.hh。
8. **synthesis → instantiation 反向 include 治理**（PRD P12）。ClockDistribution.cc + Topology.cc 直接调 DesignConversion 的问题完全未修。
9. **module/characterization 顶层 10 .hh 收敛 + Engine 禁用词清理**（PRD §G5、B11）：删除 HashJoinEngine.hh 转发头、`class TimingEngine` 改名、把 Frontier / ParetoFront / HashJoinConcat 等模板移入 `module/characterization/pareto/` 等业务语义子目录。
10. **utils → database 反向依赖根除**（PRD G7、E1）。SvgCommon.hh 直接 include design/spatial 是 PUBLIC 头依赖。

#### P2（清理面，可放下个周期）

11. **`Request` in .hh 重命名**：`ValidateRequest` / `RequestKey` 替换为 CTS 语义名。
12. **`snapshotBufferCell` 改名**（FastStaLiberty.hh:47）。
13. **`PinLocationHelper` 改名**（去 "Helper" 后缀）。
14. **`*Common.hh` 改名**（SvgCommon、RefinerCommon）。
15. **`namespace ... detail` 减少**（41 文件分 4 个 detail namespace；若按 G12 严格对齐目录则应都改为目录名 namespace）。
16. **`module/routing/database/` 空目录冗余**清理（PRD P2）。
17. **STAAdapter / AnalyticalSolver chapter slicing 治理**（PRD §6.2 同类问题外延）。
18. **fast_sta types/ 12 .hh 合并为 4-5 个**（B14）。
19. **htree/{solution, segment_pruning, embedding, plan} 子目录顶层 5/6/3/2 个悬浮 .hh** 私有化或合并（B5）。
20. **Flow.hh include 链收敛**（不再 include 4 层深的 sub-sub-flow 头作为数据成员类型）（E5）。
21. **数据结构重复实现**（PRD P11：Point / Manhattan / Bounding box / K-means / Cluster eval）—— 本次未追踪进展。

### G3 总体打分（满分 10）

| 维度 | 得分 |
|---|---|
| 命名清理（Internal/Snapshot/Wrapper/Manager/Helper class）| 7.5 / 10（清理彻底，残留 Engine + Detail + Request） |
| 暴露面收敛（顶层门面唯一性）| 5.5 / 10（fast_sta 极佳；htree/、characterization/、analytical_solver/ 仍乱） |
| Mega-class 拆分 | 4 / 10（FastClustering ✓；BST + CharBuilder 完全没动） |
| CMake target 收敛 | 1.5 / 10（102 vs 目标 35） |
| 跨层 / 跨 sub-flow 治理 | 4 / 10（Report→Evaluation 改查询契约；synthesis→instantiation 未动；utils→database 未动） |
| Sub-flow 范式统一 | 4.5 / 10（统一了 init/run/report 三方法名，但 outcome 类型未统一） |
| 公开头大小 | 6 / 10（超长 4 个，3 个有 docstring 自注） |
| **加权总评** | **约 4.5 / 10**（结构已现雏形，距 PRD §6 完整验收剩约 50% 工作量） |

---

## 9. Caveats / 调研范围声明

- 本审视**没有运行 build / test**，未验证当前代码能否编译；纯静态文件证据。
- 没有读 `.trellis/tasks/05-19-icts-code-standardization-refactor/research/` 下原 30 份调研，只读了 `prd.md` 与当前源码作为对照基线。
- 没有评估算法正确性 / 性能 / API 兼容性。
- 没有对 `src/operation/iCTS/api/`、`external_libs/`、`test/` 的现状做新调查（PRD §5.1 in-scope 但用户本次的 scope 是 `source/`）。
- 子任务 `icts-naming-convention-spec` 是否落地（生成 `.trellis/spec/backend/icts-naming-convention.md`）本次未检查，但若已落地，本文件可作为 spec 的反向 grading。
