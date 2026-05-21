# Research: 文件组织、.hh/.cc 分布与 CMake 粒度

- **Query**: 哪些 .hh 声明过多？哪些目录组织失衡？CMake target 粒度是否合理？
- **Scope**: internal
- **Date**: 2026-05-19

## 总量

- flow 层 .hh 共 **70 个**、.cc 共 **73 个**、CMakeLists.txt 共 **50 个**。
- 顶层 7 个 add_library：`icts_source_flow`、`icts_source_flow_setup`、`icts_source_flow_synthesis`、`icts_source_flow_instantiation`、`icts_source_flow_optimization`、`icts_source_flow_evaluation`、`icts_source_flow_report`。
- 二级及更深 target 共 ~43 个（粒度极细）。

## 1. 头文件声明过多 / 单文件过大

### 文件大小 Top 10（.hh）

| 文件 | 行数 | 问题 |
|---|---|---|
| `synthesis/htree/segment_pruning/SegmentLibrary.hh` | 563 | 13 个类/结构 + 多个 hash + 模板 inline 实现塞在 .hh |
| `optimization/model/OptimizationTypes.hh` | 230 | 21 个结构体 + 2 枚举 + 1 type alias |
| `synthesis/htree/HTree.hh` | 221 | 嵌套类 `LogContext/BuildOptions/LevelPlan/InsertedInstLevel/InsertedNetLevel/RootDriverCompensationReport/BuildResult` 全部在外层类内部 |
| `synthesis/htree/analytical_solver/AnalyticalSolverInternal.hh` | 206 | 12 个结构 + 20+ free function 声明 |
| `synthesis/htree/compensation/RootDriverCompensation.hh` | 169 | `RootDriverCompensationDetail/Options/Stats/ClosureCheck/ApplyResult/Pass` 6 个公开类型 + Pimpl Pass |
| `synthesis/topology/Topology.hh` | 151 | 嵌套 `BuildOptions/SourceTrunkBuildOptions/ClusterBufferMeta/...` |
| `synthesis/htree/region/SinkLoadRegion.hh` | 133 | 多个结构 + hash |
| `synthesis/htree/analytical_solver/AnalyticalCandidate.hh` | 131 | — |
| `synthesis/htree/analytical_solver/AnalyticalSelection.hh` | 128 | — |
| `synthesis/htree/compensation/RootDriverCompensationInternal.hh` | 116 | 私有头泄露 |

### 文件大小 Top 5（.cc）

| 文件 | 行数 |
|---|---|
| `synthesis/topology/trunk/SourceTrunkSegment.cc` | 573 |
| `synthesis/htree/embedding/Embedding.cc` | 567 |
| `instantiation/design_conversion/DesignConversion.cc` | 512 |
| `synthesis/htree/analytical_solver/AnalyticalSelection.cc` | 498 |
| `synthesis/htree/topology_pruning/TopologyPruning.cc` | 471 |

### 应被拆分的 .hh（建议）

1. **`segment_pruning/SegmentLibrary.hh`**（563 行）—— 内有：
   - `enum class SegmentFrontierKind` + `class SegmentFrontierKindSet`
   - `struct BufferPatternLibrary` 含 hash 与 add/find/getCompositionState
   - `struct SegmentFrontierRequest`
   - `class SegmentCandidateFrontierSet`
   - `class SegmentFrontierCatalog`
   - `struct RequiredLengthStateKey` + hash
   - `struct SegmentClosureSolution`
   - `enum class TopologyPatternNodeKind`
   - `struct TopologyPatternNode`
   - `struct TopologyPatternLibrary` 含 addSeed/addConcat/findNode/getCompositionState/materialize
   - `struct PatternSearchResult`
   - `inline auto IsBinarySourceFanoutLegal(...)`
   - `class SegmentPatternLibraryCombiner` + `class TopologyPatternLibraryCombiner`
   
   建议拆为：`SegmentFrontierKind.hh`、`BufferPatternLibrary.hh`、`SegmentFrontierCatalog.hh`、`TopologyPatternLibrary.hh`、`PatternLibraryCombiner.hh`。

2. **`optimization/model/OptimizationTypes.hh`**（230 行 21 个结构）—— 建议按主题拆分到各子目录的私有头（如 `buffer/`、`baseline/`、`action/`、`profile/`、`state/`、`topology/`、`summary/`），同时去除 `_internal` 命名空间后缀。

3. **`synthesis/htree/HTree.hh`**（221 行）—— `BuildOptions`、`LevelPlan`、`InsertedInstLevel`、`InsertedNetLevel`、`RootDriverCompensationReport`、`BuildResult` 应该拆出，因为它们被外部多处引用，嵌套类访问语法笨重（如 `HTree::BuildOptions`、`HTree::BuildResult` 在调用方四处出现）。

4. **`synthesis/topology/Topology.hh`**（151 行）—— `BuildOptions/SourceTrunkBuildOptions/ClusterBufferMeta/ClusterLeafDistanceSummary/BuildResult/SourceTrunkBuildResult/SourceTrunkStage`，建议同上拆出。

## 2. .hh 声明与 .cc 分布失衡

### 案例 A：`HTree.hh` 声明 → 多 .cc 实现

`synthesis/htree/HTree.hh` 仅声明 `class HTree`（2 个静态方法），但其下 9 个子目录各持 .hh/.cc，按"功能切片"垂直独立：

```
synthesis/htree/
├── HTree.hh / HTree.cc            # 顶层 facade
├── analytical_solver/             # 9 个 .cc + 5 个 .hh，namespace icts::htree::analytical_solver
├── characterization/              # 5 个文件（含 library/、wirelength/）
├── compensation/                  # 4 个文件（Pimpl Pass + Internal 头）
├── constraint/                    # 1 hh + 1 cc
├── embedding/                     # 1 cc 567 行 + 2 hh
├── plan/                          # 3 cc + 2 hh（plan + depth_plan + 报表）
├── region/                        # 1 hh 133 行 + 1 cc 407 行
├── segment_pruning/               # 2 hh + 1 cc（SegmentLibrary 是 hh-only）
├── solution/                      # 4 cc + 4 hh
└── topology_pruning/              # 2 cc + 1 hh
```

`HTree::build` 在 `HTree.cc:78-461` 实现，内部交叉调用上述 9 个子目录的 free function/类，构成 sub-sub-flow，但**没有统一入口**——它们由 HTree.cc 自由组合，没有任何接口抽象隔离 sub-sub-flow 边界。

### 案例 B：`Topology.hh` 声明 → 入口实现分散

`Topology::build` 在 `Topology.cc:350-353` 仅一行转发：`return topology::BuildSinkTree(root_net, options);`，真正实现在 `synthesis/topology/sink/SinkBranch.cc`。这是合理的实现拆分，但 `Topology.hh` 没在 doc 注释中说明"实际实现在哪个子文件"，跨文件追踪困难。

### 案例 C：`DesignConversion.hh` 声明在 instantiation 目录但实现分散

`instantiation/design_conversion/DesignConversion.hh` 声明了 11 个静态方法，实现分散在 `DesignConversion.cc`（512 行）和 `DesignConversionClockData.cc`（279 行）。这两个 .cc 都属于同一目标但按"功能领域"分文件，没有命名规则说明（如 `*ClockData.cc` 等）。

### 案例 D：`OptimizationCandidates.hh` 一个头 → 两个 .cc

`optimization/candidate/OptimizationCandidates.hh` 唯一头文件，实现却分散在 `OptimizationCandidates.cc`（349 行）和 `OptimizationScalableCandidates.cc`（451 行）—— **第二个 .cc 不在头文件中暴露任何独有 free function，但 cmake 中两者一起编译**。`Scalable` 版本与 `Exact` 版本应否在头文件层显式分开？是命名问题。

## 3. CMake target 粒度

### 当前粒度极细（每个子目录 1 target）

`optimization/` 下 8 个子目录都各自是独立 cmake target：

```
icts_source_flow_optimization
├── icts_source_flow_optimization_candidate
├── icts_source_flow_optimization_model
├── icts_source_flow_optimization_mutation
├── icts_source_flow_optimization_options
├── icts_source_flow_optimization_preparation
├── icts_source_flow_optimization_report
├── icts_source_flow_optimization_solver
└── icts_source_flow_optimization_state
```

每个 target 平均 1 个 .cc 文件，target 链接图复杂（candidate target PUBLIC 链 fast_sta + model，PRIVATE 链 options + preparation + state + utils），但实际它们都是 `namespace icts::optimization_internal`，本质是同一个内聚模块**人为切碎了 8 份**。

**问题**：
- 增加 CMake 维护成本（每加 1 个 .cc 要选 target、维护 link）。
- 链接顺序敏感：例如 `candidate` PUBLIC 依赖 `model`，但 `solver` 也依赖 `model`，需要小心避免循环。
- 增加构建配置噪声（如 `DEBUG_ICTS_SOURCE_FLOW_OPTIMIZATION` 仅在顶层 target 生效，子 target 无 debug 选项）。

### htree 下也极细

`synthesis/htree/` 下 10 个子目录全部独立 target（每个 1 add_library），加上 `characterization/library/` 和 `characterization/wirelength/` 嵌套 2 个，共 12 个 sub-target。

### 建议（粒度收敛）

将 cmake target 与 sub-flow 边界对齐：

| 建议 target | 包含子目录 |
|---|---|
| `icts_cts_setup` | `setup/` |
| `icts_cts_synthesis_core` | `synthesis/Synthesis.cc` + `distribution/` + `trace/`（不下钻 htree/topology） |
| `icts_cts_synthesis_htree` | `synthesis/htree/` 全部（不再每个子目录独立 target，改为同 target 不同 .cc） |
| `icts_cts_synthesis_topology` | `synthesis/topology/` 全部 |
| `icts_cts_instantiation` | `instantiation/` 全部 |
| `icts_cts_optimization` | `optimization/` 全部（合并 8 子目录） |
| `icts_cts_evaluation` | `evaluation/` 全部 |
| `icts_cts_report` | `report/` 全部（含 visualization 子集） |

**理由**：
- htree 和 optimization 内部的子目录是"实现细节"，对外不暴露独立 API；它们用 `_internal` namespace 已经表明不希望对外公开，那么独立 target 没有意义。
- 收敛后 CMake 文件可以从 50 减到 ~10 个，target 数量从 43 减到 ~8 个。

## 4. 当前 flow/CMakeLists.txt 顶层组织

```cmake
add_subdirectory(${ICTS_FLOW_EVALUATION})
add_subdirectory(${ICTS_FLOW_REPORT})
add_subdirectory(${ICTS_FLOW_INSTANTIATION})
add_subdirectory(${ICTS_FLOW_OPTIMIZATION})
add_subdirectory(${ICTS_FLOW_SETUP})
add_subdirectory(${ICTS_FLOW_SYNTHESIS})

add_library(icts_source_flow ${ICTS_FLOW}/Flow.cc)
```

- 顶层无 sub-flow 顺序约束（CMake 按目录加，与运行时阶段顺序无关），但 Flow.cc 的 link 表写法**PUBLIC link evaluation/instantiation/synthesis + characterization_library，PRIVATE link 其它**——揭示了"对外暴露的 Result 类型"由 evaluation/instantiation/synthesis 决定，但 Flow.hh 包含的头还有 `evaluation/qor/QorEvaluation.hh` 和 `synthesis/htree/characterization/library/CharacterizationLibrary.hh`，**说明 Flow.hh 公共契约依赖了 sub-sub-flow 的内部类型**。

## 5. include 路径与 namespace 不对齐

| 路径 | namespace |
|---|---|
| `synthesis/Synthesis.hh` | `icts` |
| `synthesis/distribution/ClockDistribution.hh` | `icts` |
| `synthesis/htree/HTree.hh` | `icts`（不是 `icts::htree`！） |
| `synthesis/htree/plan/Plan.hh` | `icts::htree` |
| `synthesis/htree/region/SinkLoadRegion.hh` | `icts`（不是 `icts::htree`） |
| `synthesis/htree/segment_pruning/SegmentLibrary.hh` | `icts::htree` |
| `synthesis/htree/analytical_solver/AnalyticalSolver.hh` | `icts::analytical` + `icts::htree::analytical_solver`（同文件两个） |
| `synthesis/htree/solution/AnalyticalSolution.hh` | `icts` → `htree` → `htree::analytical_solution` |
| `synthesis/topology/Topology.hh` | `icts` |
| `synthesis/topology/trunk/SourceTrunk.hh` | `icts::topology` |
| `synthesis/topology/trunk/SourceTrunkSegment.hh` | `icts::topology` |
| `synthesis/trace/SynthesisTrace.hh` | `icts` |
| `synthesis/trace/layout/ClockLayoutAdapter.hh` | `icts` |
| `optimization/Optimization.hh` | `icts` |
| `optimization/*` 8 个子目录 | `icts::optimization_internal` |
| `evaluation/Evaluation.hh` | `icts` |
| `evaluation/qor/QorEvaluation.hh` | `icts` |
| `evaluation/qor/QorEvaluationInternal.hh` | `icts::qor_evaluation` |
| `report/Report.hh` | `icts` |
| `report/visualization/*` | `icts::visualization` |

**这是最严重的不一致**：同一物理目录下文件用不同 namespace；同一 sub-flow 顶层用 `icts`，子目录用 `icts::xxx_internal`。

## Caveats / 不确定项

- 是否要为每个 sub-flow 引入独立 namespace（如 `icts::synthesis`、`icts::optimization`），需在 spec 中先约定。
- 当前 target 细粒度可能是历史 PR 演进结果，合并 target 时要确认 `target_link_libraries` 中没有跨子模块的依赖循环。
