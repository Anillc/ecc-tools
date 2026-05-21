# iCTS 代码规范化重构 · 技术设计 (design.md)

> **关联**：本设计承接 `prd.md` 第 4 节目标 G1~G12，给出技术方案、契约与决策依据。
> **不在本文档**：分阶段执行清单（见 `implement.md`）、问题清单原始证据（见 `research/`）。

---

## 1. 总体设计原则

| 编号 | 原则 | 解释 |
|---|---|---|
| D1 | **领域语义优先** | 所有公开符号、文件、目录命名都用 CTS / EDA 领域术语（skew / sink / driver / buffer / arrival / slew / cap / load / wirelength / pattern / Steiner / DME / DMP / Pi / Ceff …）。禁止 SaaS/Web/通用框架术语（snapshot / Internal / Wrapper / Helper / Manager / Engine 等）。 |
| D2 | **门面 + 子目录隔离** | 每个大模块对外只暴露一个 `Facade.hh`（+ 必要的 `types/` POD 头）；私有实现按职责拆为子目录 + 独立 cmake target，通过 PRIVATE include 隔离。**杜绝用 `*Internal.hh` 命名约定做软隔离**。 |
| D3 | **范式化 sub-flow** | 所有 sub-flow 实现统一生命周期契约：`prepare → execute → finalize`（详见 §3）。新 sub-flow 必须按范式注册到 `Flow` 编排器。 |
| D4 | **声明与实现分离** | 公开 .hh 只声明对外契约：类型、签名、文档注释。**严禁**在公开 .hh 暴露：① 算法私有 nested type；② 私有方法；③ 私有数据成员（用 Pimpl 或前向声明）；④ inline 实现带 `LOG_FATAL_IF`。 |
| D5 | **CMake target 与目录边界对齐** | 1 个目录 = 1 个 cmake target（除非该目录是 aggregator）；INTERFACE 库仅用于 header-only POD；目录命名不能与"上层 aggregator 占位符 + 实际打包不同内容"矛盾。 |
| D6 | **namespace ↔ 目录对齐** | `icts::<feature>::<sub>` 必须对应物理路径 `<feature>/<sub>/`；禁止 `_internal` 后缀；私有 namespace 用 `detail` 子命名空间或匿名 namespace。 |
| D7 | **零行为变更** | 重构只改外形（命名 / 文件分布 / 目录结构 / CMake / namespace），不改算法语义。所有现有测试必须保持 bit-identical 输出。 |
| D8 | **可分批 + 可回滚** | 每个 child task 是一个独立 PR；每个 commit 内只做一件事（命名 / 文件移动 / cmake / 行为）；任何阶段失败可单独 revert。 |

---

## 2. 目标分层模型

```
              ┌──────────────────────────────────┐
   外部调用方  │   src/interface/{tcl,python}/    │
              │   src/platform/tool_manager/     │
              │   src/feature/builder/           │
              └─────────────────┬────────────────┘
                                │  (5 个公开方法)
                          ┌─────▼──────┐
                          │  CTSApi    │  api/ (无子目录，无 namespace 分层)
                          └─────┬──────┘
                                │
                  ┌─────────────▼──────────────┐
                  │  flow/  (CTS 主流程编排)    │
                  │  ┌────────────────────┐    │
                  │  │ FlowOrchestrator   │    │
                  │  └────────┬───────────┘    │
                  │           │ 6 SubFlow      │
                  │  setup/synthesis/instantiation/optimization/evaluation/report
                  └───────────┬────────────────┘
                              │
              ┌───────────────▼────────────────┐
              │  module/  (CTS 算法模块)        │
              │   topology / routing / timing  │
              │   characterization / analytical│
              └───────────────┬────────────────┘
                              │
              ┌───────────────▼────────────────┐
              │  database/  (CTS 数据 + 适配)   │
              │   design / config / io / qor   │
              │   spatial / routing / timing   │
              │   characterization             │
              │   adapter/ (fast_sta / sta / sdc)
              └───────────────┬────────────────┘
                              │
              ┌───────────────▼────────────────┐
              │  utils/  (跨业务通用工具)       │
              │   spatial / graph / logger      │
              │   geometry / report_format      │
              └────────────────────────────────┘
```

**变更**：
- `utils/visualization/` 移到 `flow/report/visualization/` 或 `module/visualization/`（依调研结论选其一），不再 link database
- `utils/geometry/` 与 `database/spatial/` 边界重画：`Point<T>/Rect<T>/Region<T>` 模板类移到 `utils/spatial/`，`database/spatial/` 只保留 CTS 专用别名与 `Tree`（Tree 改为不依赖 `Pin*` 的纯拓扑结构，或下沉到 `database/design/` 因其本质是 design 视图）
- `utils/logger/` 拆为 `utils/log/`（轻 logger）+ `utils/report_format/`（表格 + Schema 输出）

---

## 3. flow 层范式：`SubFlow` 契约

### 3.1 范式选型决策

| 方案 | 优势 | 劣势 | 决策 |
|---|---|---|---|
| A. 抽象基类 `ISubFlow { virtual prepare/execute/finalize }` | 显式契约、运行时多态 | 引入虚函数开销、静态门面被破坏 | 不选 |
| B. **Concept / CRTP 静态范式 + 注册表** | 0 运行时开销、保留静态门面风格、编译期检查 | 实现稍复杂、调试时 trace 跨多层 | **选** |
| C. Free-function 范式（`PrepareXxx / RunXxx / FinalizeXxx`） | 改动最小 | 仍然零纪律性 | 不选 |
| D. Step machine（声明式 stage list） | 可视化好、易扩展 | 与现有线性 runCTS 偏离大 | 不选 |

**采纳方案 B**：每个 sub-flow 是 `class XxxFlow { static prepare/execute/finalize ... }`，由 `FlowOrchestrator` 模板化地调用；不强制虚函数，但用 C++ concept 检查接口符合范式。

### 3.2 SubFlow 契约（伪代码）

```cpp
// flow/SubFlowContract.hh (新增)
namespace icts {

template <typename T>
concept SubFlowContract = requires(typename T::Context& ctx,
                                   typename T::Input const& in,
                                   typename T::Output& out) {
  { T::name() } -> std::convertible_to<std::string_view>;
  { T::prepare(ctx, in) } -> std::same_as<bool>;       // 返回 ready
  { T::execute(ctx, in, out) } -> std::same_as<SubFlowOutcome>;
  { T::finalize(ctx, out) } -> std::same_as<void>;
};

enum class SubFlowOutcome { kFinished, kSkipped, kFailed };

struct SubFlowContext {
  ClockLayout* clock_layout = nullptr;
  CharLibrary* char_library = nullptr;
  EvaluationState* evaluation_state = nullptr;
  schema::StageHandle stage_handle;
  // … 共享数据
};

}  // namespace icts
```

每个 sub-flow：

```cpp
// flow/synthesis/Synthesis.hh
namespace icts::synthesis {

class SynthesisFlow {
 public:
  using Context = SubFlowContext;
  using Input   = SynthesisInput;
  using Output  = SynthesisReport;

  SynthesisFlow() = delete;

  static constexpr auto name() -> std::string_view { return "Synthesis"; }
  static auto prepare(Context& ctx, const Input& in) -> bool;
  static auto execute(Context& ctx, const Input& in, Output& out) -> SubFlowOutcome;
  static auto finalize(Context& ctx, Output& out) -> void;
};

}  // namespace icts::synthesis
```

**统一规则**：
- 每个 sub-flow 都有 `prepare/execute/finalize` 三阶段（即使某阶段是 no-op 也要声明，便于阅读者快速定位）
- `prepare` 返回 bool：true 表示可以继续，false 表示跳过（不是失败）
- `execute` 返回 `SubFlowOutcome` 三态：finished / skipped / failed
- `finalize` 用于 schema 表汇总、metric 写出、状态清理，**总是被调用**（无论 execute 是否失败）
- Setup 的 `initialize` / `emitRuntimeSetup` 重命名为 `prepare` / `finalize`（execute 是 no-op）
- Report 的 "如果 Evaluation 没做就补做" 反向调用消除：改为 `prepare` 检查 ready 状态，由 Orchestrator 决定是否回到 Evaluation
- Optimization 的 145 行 `run` 内嵌 12 步 → 拆为 `OptimizationStages`，每个 stage 是 `SubFlowContract` 实现

### 3.3 FlowOrchestrator

```cpp
// flow/FlowOrchestrator.hh
namespace icts {

class FlowOrchestrator {
 public:
  static auto getInst() -> FlowOrchestrator&;

  template <SubFlowContract Flow>
  auto runSubFlow(typename Flow::Input&& in) -> typename Flow::Output;

  auto runCTS() -> void;
  auto report(const std::string& save_dir) -> void;
  auto reset() -> void;
  // ...
};

}  // namespace icts
```

- 替代原 `class Flow`（单例保留）
- 公共 API 数量不变（兼容 CTSApi）
- 内部把 setup→synthesis→optimization→instantiation→evaluation→report 用 `runSubFlow<...>` 模板调用统一起来
- Schema stage 命名固化：`name()` 直接来源于 sub-flow，不再出现 "CTSFlow" 混淆名

### 3.4 sub-flow 物理拆分

```
flow/
├── FlowOrchestrator.hh / .cc       (新)
├── SubFlowContract.hh              (新，concept + Context)
├── SubFlowSchemaIntegration.hh     (新，stage/metric RAII)
├── setup/
│   ├── SetupFlow.hh / .cc           (改名自 Setup.hh)
│   └── ...
├── synthesis/
│   ├── SynthesisFlow.hh / .cc       (改名自 Synthesis.hh)
│   ├── htree/                       (内部 sub-sub-flow，自己也走 SubFlowContract)
│   ├── topology/
│   ├── distribution/
│   └── trace/
├── instantiation/
│   ├── InstantiationFlow.hh / .cc
│   ├── idb_conversion/
│   └── (design_conversion/ 移出 → flow/setup/ 或 database/io/，因其被多 sub-flow 共用)
├── optimization/
│   ├── OptimizationFlow.hh / .cc
│   └── stages/                       (替代 8 个平级子目录)
│       ├── PrepareStage.hh / .cc
│       ├── BaselineStage.hh / .cc
│       ├── SolverStage.hh / .cc
│       ├── MutationStage.hh / .cc
│       └── ReportStage.hh / .cc
├── evaluation/
│   ├── EvaluationFlow.hh / .cc       (砍掉 5 个静态入口，保留 prepare/execute/finalize)
│   └── qor/
└── report/
    ├── ReportFlow.hh / .cc
    └── ...
```

---

## 4. fast_sta 拆分方案

### 4.1 目标目录树

完全采纳调研提案（`research/database/04-fast_sta-refactor-proposal.md`）：

```
database/adapter/fast_sta/
├── CMakeLists.txt                # INTERFACE 聚合
├── FastSta.hh / FastSta.cc       # 唯一对外门面（17 个 API，删除 5 个未用 API + 移 clear 到 friend）
├── types/                        # POD 头（INTERFACE 库）
│   ├── FastStaIds.hh
│   ├── FastStaEnums.hh
│   ├── FastStaGeometry.hh
│   ├── FastStaLibertyTypes.hh
│   ├── FastStaParasiticTypes.hh
│   ├── FastStaTimingTypes.hh
│   ├── FastStaPowerTypes.hh
│   ├── FastStaNodeNet.hh
│   ├── FastStaContext.hh
│   ├── FastStaIncrementalTypes.hh
│   ├── FastStaStatusTypes.hh
│   ├── FastStaCharTypes.hh
│   └── FastStaTypes.cc           # lookup() 实现
├── liberty/                      # 从 iSTA Liberty 提取
├── parasitics/                   # RC reduction（最大 .cc，拆 3 个）
├── dmp_ceff/                     # DMP effective-cap solver
│   ├── FastStaDmpCeff.hh         (公开)
│   ├── FastStaDmpCeff.cc
│   ├── DmpSolver.hh              (子目录私有，取代 Internal.hh)
│   ├── DmpSolver.cc
│   ├── DmpSolverEquations.cc
│   ├── DmpLibertyLookup.hh
│   ├── DmpLibertyLookup.cc
│   ├── DmpNumerics.hh
│   └── DmpNumerics.cc
├── clock_tree/                   # 从 design::Clock 构建拓扑
├── builder/                      # 协调初始化（最依赖）
├── incremental/                  # buffer master 替换 + 标脏域
├── timing/                       # 拓扑序时序传播
├── power/                        # 功耗求和
└── report/                       # 日志摘要
```

### 4.2 关键决策

| 子目录 | CMake target 类型 | PUBLIC | PRIVATE |
|---|---|---|---|
| `types/` | INTERFACE | 所有 POD 头 | — |
| `liberty/` | STATIC | `FastStaLiberty.hh` | 调用 STAAdapter |
| `parasitics/` | STATIC | `FastStaParasitics.hh` | 调用 STAAdapter |
| `dmp_ceff/` | STATIC | `FastStaDmpCeff.hh` | `DmpSolver.hh` / `DmpLibertyLookup.hh` / `DmpNumerics.hh` |
| `clock_tree/` | STATIC | `FastStaClockTree.hh` | parasitics + design + spatial |
| `builder/` | STATIC | `FastStaBuilder.hh` / `FastStaCharBuilder.hh` | liberty + parasitics + clock_tree + timing + power + design + io + config + sta_adapter |
| `incremental/` | STATIC | `FastStaIncremental.hh` | liberty |
| `timing/` | STATIC | `FastStaTiming.hh` | parasitics + dmp_ceff |
| `power/` | STATIC | `FastStaPower.hh` | — |
| `report/` | STATIC | `FastStaReport.hh` | — |
| `fast_sta`（顶层）| STATIC | `FastSta.hh` | 所有子目录 |

### 4.3 命名规范化（fast_sta 内部）

| 当前 | 改为 | 备注 |
|---|---|---|
| `FastStaDmpCeffInternal.hh` | 拆为 `DmpSolver.hh` + `DmpLibertyLookup.hh` + `DmpNumerics.hh` | 文件描述对象，不描述可见性 |
| `FastStaDmpCeffShared.cc` | `DmpNumerics.cc` + `DmpLibertyLookup.cc` | "Shared" 是命名约定 |
| `FastStaLiberty::snapshotBufferCell` | `extractBufferCellLiberty` | snapshot → extract |
| `FastStaLiberty::snapshotTable/DelayTable/PowerTable/PowerArcTables` | `extractTable/DelayTable/...` | 同上 |
| `FastStaBuilder::snapshotClockData` | `materializeClockData` | snapshot → materialize |
| `FastStaBuilder::snapshotSinkPinCaps` | `assignSinkPinCaps` | snapshot → assign |
| `FastStaSlewRole` | `FastStaSlewKind` | 与 `FastStaNodeKind` 对齐 |
| `FastStaDirtyRegion` | `FastStaInvalidatedScope` | dirty 是图形/缓存术语 |
| 注释里 "Liberty data snapshots extracted" | "Liberty data captured for CTS cells" | — |

### 4.4 修复门面破例

- `FastSta.hh` 新增：`static auto injectNetRouteTree(FastStaClockId, FastStaNetId, RouteTree const&) -> void`
- 删除 `flow/optimization/preparation/OptimizationPreparation.cc:271` 直接 include `FastStaBuilder.hh`，改走 `FastSta.hh`

### 4.5 删除未使用 API

`FastSta.hh` 21 → 17：
- 删除：`changeBufferMaster(单点)`、`rebuildClockContext`、`querySinkArrival`、`queryNodeSlew`、`queryNetLoad`、`queryArea`、`queryClockIds`
- 移到 friend：`clear()` 仅供 `FastStaTestAccess` 使用

### 4.6 类型类暴露面

外部（flow / module）只可见：
```
FastSta.hh
types/FastStaIds.hh
types/FastStaEnums.hh
types/FastStaContext.hh
types/FastStaTimingTypes.hh
types/FastStaStatusTypes.hh
types/FastStaPowerTypes.hh
types/FastStaIncrementalTypes.hh
types/FastStaCharTypes.hh
types/FastStaNodeNet.hh         (transitive)
types/FastStaParasiticTypes.hh  (transitive)
types/FastStaLibertyTypes.hh    (transitive)
types/FastStaGeometry.hh        (transitive)
```

---

## 5. Mega-class 拆分规范（适用于 BST / CharBuilder / FastClustering）

### 5.1 通用原则

| 当前模式 | 替换为 |
|---|---|
| 单类 `ClassName` + N 个 `ClassNameTopic.cc` 章节式拆分 | 阶段类拆分：每个 .cc 是一个独立类的 `run`/`process` 实现 |
| .hh 暴露 11+ 个私有 nested struct | `detail/` 子目录中独立 .hh 持有；公开头只前向声明 |
| .hh 暴露 80+ 个 private method | Pimpl 或拆为多个组件类 |
| 所有数据成员塞在 .hh | Pimpl（unique_ptr<Impl>），实现侧持有 |

### 5.2 BoundSkewTree 拆分

```
module/routing/bound_skew_tree/
├── CMakeLists.txt                   # INTERFACE 聚合
├── BstRouter.hh / BstRouter.cc      # 公开门面（改名自 BSTRouter）
├── BstParameters.hh                 # 公开类型（改名自 BSTTypes.hh）
├── BstSteinerTree.hh                # 公开类型（如果暴露给 cluster_constraints）
├── geometry/                        # GeomCalc 拆分 → 新子目录
│   ├── BstPoint.hh / .cc            (改名自 Components.hh::Point)
│   ├── BstArea.hh / .cc             (改名自 Components.hh::Area)
│   ├── BstMatch.hh                  (改名自 Components.hh::Match)
│   ├── BstInterval.hh / .cc         (改名自 Components.hh::Interval)
│   ├── BstTrr.hh / .cc              (改名自 Components.hh::TransformedRect, Tilted Rotated Rectangle)
│   ├── BstGeomCalc.hh               (公开接口)
│   ├── BstGeomCalcLine.cc
│   ├── BstGeomCalcPointRegion.cc
│   └── BstGeomCalcTransformedRect.cc
├── algorithm/                       # BoundSkewTree mega-class 拆分
│   ├── BoundSkewTree.hh             (公开门面，Pimpl)
│   ├── BoundSkewTree.cc             (Pimpl + run() 编排)
│   ├── detail/
│   │   ├── BoundSkewTreeImpl.hh     (Pimpl impl，私有 nested types)
│   │   ├── BstTopologyBuilder.hh / .cc      (替代 BoundSkewTreeTopology.cc)
│   │   ├── BstJoiningSolver.hh / .cc        (替代 BoundSkewTreeJoining.cc)
│   │   ├── BstBalanceSolver.hh / .cc        (替代 BoundSkewTreeBalance.cc)
│   │   ├── BstEmbeddingSolver.hh / .cc      (替代 BoundSkewTreeEmbedding.cc)
│   │   ├── BstInfeasibleMergeSolver.hh / .cc (替代 BoundSkewTreeInfeasibleMerge.cc)
│   │   └── BstBottomUpTopDownDriver.hh / .cc (替代 BoundSkewTreeFlow.cc，去 Flow 字眼)
└── adapter/                         # BSTRouter -> Router 桥接
    ├── BstClockTreeExport.hh / .cc      (改名自 BSTRouterExport.cc)
    └── BstInputTopologyBuilder.hh / .cc (改名自 BSTRouterBinaryTopology.cc)
```

### 5.3 CharBuilder 拆分

```
module/characterization/
├── CMakeLists.txt
├── CharBuilder.hh / CharBuilder.cc     # 公开门面，Pimpl，仅 init/build/get_xxx
├── detail/
│   ├── CharBuilderImpl.hh / .cc
│   ├── CircuitBuilder.hh / .cc          (替代 CharBuilderCircuit.cc)
│   ├── PatternEnumerator.hh / .cc       (替代 CharBuilderPatternEnumeration.cc)
│   ├── PatternStorage.hh / .cc
│   ├── SampleStorage.hh / .cc
│   ├── SlewSampler.hh / .cc
│   ├── CapSampler.hh / .cc
│   ├── StaSampler.hh / .cc              (改名 CharBuilderStaSampling.cc)
│   ├── TopologyPlanner.hh / .cc         (改名 CharBuilderTopology.cc)
│   └── FeasibilityChecker.hh / .cc
├── BufferingPattern.hh                  (公开)
├── HTreeTopologyChar.hh                 (公开)
├── SegmentChar.hh                       (公开)
├── ParetoFront.hh                       (改名自 Frontier.hh)
├── ParetoFront.cc                       (从 .hh 下沉的大方法)
├── HashJoinConcat.hh                    (改名自 HashJoinEngine.hh, 不再叫 Engine)
└── PatternCombiner.hh                   (保留，命名 OK)
```

### 5.4 FastClustering 拆分

```
module/topology/fast_clustering/
├── CMakeLists.txt
├── FastClustering.hh / FastClustering.cc    # 公开门面（保留 run/runDefault/buildElectricalBaseConfig）
├── detail/                                  # 取代 FastClusteringInternal.hh
│   ├── ClusterCandidate.hh                  # 改名 ClusterDraft
│   ├── ClusterCandidateStats.hh             # 改名 DraftAggregate
│   ├── ClusterBounds.hh                     # 改名 Bounds（避免与 BST 冲突）
│   ├── NeighborGraph.hh
│   ├── CrossBoundaryMove.hh                 # 改名 BoundaryMove
│   ├── ClusterRefiner.hh / .cc              # 改名 *Polish.cc
│   ├── MergeRefiner.hh / .cc                # 改名 *MergePolish.cc
│   ├── BoundaryRefiner.hh / .cc             # 改名 *BoundaryPolish.cc
│   ├── ClusterPartitioner.hh / .cc          # 替代 *Partition.cc
│   ├── ClusterFinalizer.hh / .cc            # 替代 *Finalize.cc
│   ├── ClusterGeometry.hh / .cc             # 替代 *Geometry.cc
│   ├── BoundaryCandidates.hh / .cc
│   └── BoundarySearch.hh / .cc
```

---

## 6. 命名规范（核心 spec）

### 6.1 禁用词表（source 内）

| 禁用 | 例外 | 替代方向 |
|---|---|---|
| `Internal`（类名/文件名/namespace 后缀） | 无 | 改用 `detail` 子命名空间或匿名 namespace；私有 .hh 用业务名 |
| `Snapshot`（除注释外） | `RuntimeMetricSample`（运行指标这一类样本） | `Extract*` / `Capture*` / `Materialize*` |
| `Wrapper` | 无 | `Adapter` / `Bridge` / `Facade` / `IdbAccess` |
| `Helper`（类/文件/目录名） | 注释里描述"辅助"可保留 | 按内容命名（如 `PinLocationLookup`） |
| `Manager` / `Handler` / `Service` / `Provider` | 无 | 按职责命名（`Driver` / `Scheduler` / `Propagator`） |
| `Engine`（除领域已固化） | `TimingEngine` 在 STA 语境下保留 | `Propagator` / `Solver` / `Evaluator` |
| `Util` / `Common`（类/文件名） | 注释保留 | 按内容命名 |
| `Request` / `Response` | 无 | `Input` / `Output` / `Query` / `Constraints` / `Context` |
| `Support`（source 内）| 无 | 按内容命名 |
| `Polish`（重构/精修） | 无 | `Refine` / `Rebalance` / `LocalImprove` |
| `Draft` | 无 | `Candidate` |
| `Desc`（缩写） | 无 | `Plan` / `Spec` / `Definition` |
| `Frontier`（搜索语境）| 无 | `ParetoFront` / `EfficiencyFront` |

### 6.2 test 内的 *Support* 替代规则

| test 内 | 改为（按文件实际作用） |
|---|---|
| 单纯 fixture（gtest TestF） | `*Fixture.hh` |
| 测试数据/资产 | `*Asset.hh` / `*Sample.hh` |
| 测试构造器 | `*Builder.hh`（真正构造测试场景） |
| 测试场景脚本 | `*Scenario.hh` |
| 测试通用工具 | `*Util.hh` 仅 test 内允许（test 内放宽） |

### 6.3 单例宏命名

| 当前 | 改为 |
|---|---|
| `CONFIG_INST` | `CTS_CONFIG_INST` |
| `DESIGN_INST` | `CTS_DESIGN_INST` |
| `WRAPPER_INST` | `IDB_BRIDGE_INST`（Wrapper 重命名为 IdbBridge 后） |
| `FLOW_INST` | `CTS_FLOW_INST` 或保留（无歧义） |
| `STA_ADAPTER_INST` | 保留 |
| `FAST_STA_INST` | 保留 |
| `SCHEMA_WRITER_INST` | `CTS_REPORT_INST`（如果 Schema 重命名）或保留 |

### 6.4 namespace 规则

| 规则 | 描述 |
|---|---|
| 1 | 顶层 namespace 是 `icts` |
| 2 | 子模块 namespace 是 `icts::<feature>`，对应物理路径 `<feature>/` |
| 3 | 子子模块 namespace 是 `icts::<feature>::<sub>`，对应 `<feature>/<sub>/` |
| 4 | 私有命名空间统一用 `detail`（如 `icts::synthesis::detail`）；**禁用** `_internal` 后缀 |
| 5 | 跨文件共享的 .cc 内部 helper 用匿名 namespace |
| 6 | 模板的辅助函数可放 `icts::<feature>::detail` |

### 6.5 类型命名

| 规则 | 描述 |
|---|---|
| 1 | 类/struct/enum/typedef 用 PascalCase |
| 2 | enum class 值用 `kXxx` |
| 3 | 不嵌套 `BuildOptions`/`BuildResult` 在外层类内部（提升为同 namespace 顶层类型并加前缀，如 `HTreeBuildOptions`） |
| 4 | Id 类型用 `using XxxId = std::size_t` + `constexpr XxxId kInvalidXxxId = ...` 对子 |
| 5 | 公开类型名包含业务上下文（避免 `BuildContext`、`Options` 这类无前缀名） |

---

## 7. CMake target 收敛策略

### 7.1 收敛目标

| 区域 | 现有 target 数 | 收敛后 target 数 |
|---|---:|---:|
| `flow/` | 43 | ~8（每 sub-flow 一个） |
| `module/routing/` | 8 | ~3（router aggregator + bst + cbs/flute/salt/local_legalization 合并） |
| `module/topology/` | 6 | ~3（topology + clustering(含 fast_clustering) + constraints） |
| `module/characterization` | 1 | 1 |
| `module/analytical_characterization` | 1 | 1 |
| `module/timing` | 1 | 1 |
| `database/adapter/fast_sta` | 1 | 11（拆开） |
| `database/adapter/sta` | 1 | ~3（按 internals 拆分） |
| `database/adapter/sdc` | 1 | ~2（parser + clock_trace） |
| `database/{config,design,io,...}` | 8 | 8（保持） |
| `utils/` | 4 | 4 或 5（视 visualization 处理） |
| **合计** | **~78** | **~38** |

### 7.2 公共 CMake 宏

新增 `cmake/icts_targets.cmake`：

```cmake
function(icts_apply_debug_flags target debug_option_name)
  if(${debug_option_name})
    target_compile_options(${target} PRIVATE -g3 -O0 -fno-inline -fno-omit-frame-pointer)
  endif()
endfunction()

function(icts_add_library target_name)
  cmake_parse_arguments(ARG "INTERFACE" "DEBUG_OPTION" "SOURCES;PUBLIC_LINKS;PRIVATE_LINKS;PUBLIC_INCLUDES;PRIVATE_INCLUDES" ${ARGN})
  if(ARG_INTERFACE)
    add_library(${target_name} INTERFACE)
    target_include_directories(${target_name} INTERFACE ${ARG_PUBLIC_INCLUDES})
    target_link_libraries(${target_name} INTERFACE ${ARG_PUBLIC_LINKS})
  else()
    add_library(${target_name} ${ARG_SOURCES})
    target_include_directories(${target_name}
      PUBLIC ${ARG_PUBLIC_INCLUDES}
      PRIVATE ${ARG_PRIVATE_INCLUDES})
    target_link_libraries(${target_name}
      PUBLIC ${ARG_PUBLIC_LINKS}
      PRIVATE ${ARG_PRIVATE_LINKS})
    if(ARG_DEBUG_OPTION)
      icts_apply_debug_flags(${target_name} ${ARG_DEBUG_OPTION})
    endif()
  endif()
endfunction()
```

### 7.3 utils → database 反向治理

| 子模块 | 处理 |
|---|---|
| `utils/geometry/Geometry.hh` | 将依赖的 `Point<T>/Rect<T>/Region<T>` 从 `database/spatial/` 上移到 `utils/spatial/`，`database/spatial/` 改为 CTS 业务特化（如 `Tree`）。`utils/geometry` 与 `utils/spatial` 都不再 link database |
| `utils/visualization/` | 移动到 `flow/report/visualization/` 或新建 `module/visualization/`（依调研结论，决策见 §10）。`utils/` 完全不引用 `design/Pin.hh`。 |
| `database/spatial/Tree.hh` | 改为不依赖 `Pin*` 的纯拓扑 `Tree<NodeT>` 模板；CTS 专用 alias 放 `database/design/` 或保留在 `database/spatial/` 但用前向声明 |

---

## 8. 单例 reset 契约

### 8.1 IResettable 接口

```cpp
// utils/singleton/IResettable.hh
namespace icts {

class IResettable {
 public:
  virtual ~IResettable() = default;
  virtual auto reset() -> void = 0;
  virtual auto singletonName() const -> std::string_view = 0;
};

class SingletonRegistry {
 public:
  static auto getInst() -> SingletonRegistry&;

  auto registerSingleton(IResettable* singleton) -> void;
  auto resetAll() -> void;          // 按注册顺序 reset
  auto resetAllReversed() -> void;  // 按反序 reset（构造与析构 LIFO）

 private:
  std::vector<IResettable*> _singletons;
};

}  // namespace icts
```

### 8.2 每个单例的处理

```cpp
class FastSTA final : public IResettable {
 public:
  static auto getInst() -> FastSTA& {
    static FastSTA instance;
    static bool registered = []{
      SingletonRegistry::getInst().registerSingleton(&getInst());
      return true;
    }();
    (void)registered;
    return instance;
  }

  auto reset() -> void override { /* ... */ }
  auto singletonName() const -> std::string_view override { return "FastSTA"; }

 private:
  FastSTA() = default;
};
```

### 8.3 `CtsApi::resetAPI()` 简化

```cpp
auto CtsApi::resetAPI() -> void {
  SingletonRegistry::getInst().resetAllReversed();
}
```

新单例只需继承 `IResettable` 即可，自动加入 reset 列表。

---

## 9. 大头文件拆分策略

| 文件 | 行数 | 拆分方向 |
|---|---:|---|
| `flow/synthesis/htree/segment_pruning/SegmentLibrary.hh` | 563 | 拆 `SegmentFrontier.hh` + `BufferPatternLibrary.hh` + `TopologyPatternLibrary.hh` + `*Combiner.hh`；inline 大方法下沉 .cc |
| `database/adapter/fast_sta/FastStaTypes.hh` | 382 | 见 §4.1，拆为 13 个 types/ 头 |
| `module/routing/bound_skew_tree/BoundSkewTree.hh` | 371 | 见 §5.2，Pimpl + 私有头移 `detail/` |
| `database/routing/SteinerTree.hh` | 349 | 审视 inline accessor，保留模板但下沉非模板大方法 |
| `module/routing/bound_skew_tree/Components.hh` | 347 | 见 §5.2，5 类拆 5 个独立 .hh |
| `utils/visualization/core/SvgCommon.hh` | 323 | 拆 `SvgConstants.hh` + `SvgTransform.hh` + `SvgColorPolicy.hh` |
| `module/characterization/Frontier.hh` | 319 | 改名 `ParetoFront.hh`，拆 `FrontierKey.hh` + `FrontierPruner.hh` |
| `flow/optimization/model/OptimizationTypes.hh` | 230 | 拆 7 个子主题头（`buffer/` / `baseline/` / `action/` / `profile/` / `state/` / `topology/` / `summary/`） |
| `flow/synthesis/htree/HTree.hh` | 221 | 6 个嵌套 struct 提升为同 namespace 顶层类型 |

**规则**：拆分后 `.hh` 行数 ≤ 200，inline 实现严格限制为短小 accessor（≤5 行）。

---

## 10. 决策待审议项（design 阶段需用户拍板）

| 决策点 | 选项 | 推荐 |
|---|---|---|
| **D10.1** sub-flow 范式名 | A. `prepare/execute/finalize` · B. `init/run/report`（用户原话）· C. `setup/run/teardown` | **B**（与用户表述一致；用户原文"init/run/report"） |
| **D10.2** utils/visualization 归宿 | A. 移到 `flow/report/visualization/` 合并 · B. 移到新 `module/visualization/` · C. 保留 `utils/visualization/` 但解除 database 依赖 | **A**（与 flow/report/visualization 已重叠，合并更彻底） |
| **D10.3** `database/adapter/fast_sta/` 是否搬到 `module/` | A. 保留在 adapter（命名 = 数据 adapter）· B. 移到 `module/fast_sta/`（视作算法模块）| **A** 配合修正描述（adapter 是"对 iSTA 的轻量替代"，仍是 adapter）|
| **D10.4** `FastStaClockContext` 同时承载真实时钟与特征化电路 | A. 保留同类型（现状）· B. 拆为 `FastStaClockContext` + `FastStaCharContext` 两类型 | **A**（重构不改语义，仅记入 spec 提醒） |
| **D10.5** 是否引入 SubFlowOrchestrator | A. 引入（本方案）· B. 仍用 Flow 类内方法编排 | **A** |
| **D10.6** `Wrapper` → 改名候选 | A. `IdbBridge` · B. `CtsIdbAccess` · C. `ClockIo` | **A**（最准确） |
| **D10.7** mega-class 拆分采用 Pimpl 还是组件分解 | A. Pimpl + 1 对外 class · B. 拆为多个对外 class（Driver/Solver/...） | **A**（破坏面最小，保持单门面） |
| **D10.8** 是否引入 `IResettable` + Registry | A. 引入（本方案）· B. 仍手工 reset list | **A** |
| **D10.9** namespace 是否每 sub-flow 用独立 namespace（`icts::synthesis::*`） | A. 是 · B. 保持 `icts` 平铺，只在 detail 里分 | **A**（与目录对齐，便于 IDE 跳转）|
| **D10.10** test 内 *Support* 是否本任务范围 | A. 是（本方案）· B. 拆为独立任务 | **A**（但优先级最低）|

---

## 11. 风险与回滚

| 风险 | 缓解 | 回滚边界 |
|---|---|---|
| Pimpl 引入函数调用开销影响 BST/CharBuilder 热路径 | 拆分前 benchmark；如热路径退化 >5% 用模板/inline impl | 每个 mega-class 独立 PR，可单独 revert |
| Sub-flow 范式改造破坏 schema 输出 | bit-identical 比对：FlowTest 跑 same input 比 `schema` 输出 | 范式改造按 sub-flow 单独 PR |
| CMake 收敛引入隐藏循环依赖 | 用 `cmake --graphviz` 生成依赖图比对 | 单 PR 内回滚 |
| `IResettable` 注册顺序与析构顺序冲突 | reset 用反序；引入 `[[deprecated]]` 路径让旧调用平滑过渡 | 注册表可关闭，回 fallback 手工列表 |
| `Wrapper`→`IdbBridge` 改名波及外部 binding | 先做 alias `using Wrapper = IdbBridge;` 一个 release，再删 alias | alias 兼容期 |

---

## 12. 跨子任务一致性契约

由 parent task 维护、所有 child task 必须遵守：

1. **命名规范文档**（`icts-naming-convention-spec` 产出）是所有 child task 的输入。
2. **每个 child task PR 必须**：
   - 在 PR description 中列出该 PR 触及的禁用词清除证据（`grep -rn 'snapshot' ... 重构前 N → 重构后 M`）
   - 通过 `bash build.sh` 全量构建
   - 通过 `iCTS/test/` 现有全部测试
   - 通过 bit-identical 端到端验证（如适用）
3. **跨 child task 数据契约**：
   - `FastSta.hh` 17 个 public API 签名稳定（child `icts-fast-sta-decomposition` 锁定后，其他 task 不再改）
   - `CtsApi.hh` 5 个 public API 签名稳定（所有 child task 都不能改）
   - `Flow` 单例宏（无论改名为 `CTS_FLOW_INST` 还是保留 `FLOW_INST`）由 parent task 在 spec 中固化
4. **每个 child task 完成后**：
   - 归档到 `.trellis/tasks/` 并在本任务的 `prd.md` 第 7 节附录里标记完成日期
   - 更新本 `design.md` 第 10 节决策点状态（如有调整）

---

## 13. 接下来

→ 见 `implement.md`：分阶段、可回滚的执行清单（child task 顺序、验证手段、review gate）。
