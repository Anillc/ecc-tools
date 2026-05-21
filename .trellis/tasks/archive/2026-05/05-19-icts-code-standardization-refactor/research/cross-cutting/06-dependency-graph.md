# Research: Dependency Graph

- **Query**: iCTS 内部依赖混乱、utils 是否被错误地依赖了 module/flow/database、循环依赖、.hh 互相 include 形成依赖网
- **Scope**: internal
- **Date**: 2026-05-19

## 6.1 层级理想模型 vs 现实

理想（从下到上）：
```
external_libs → utils → database → module → flow → api
```

实际（CMake 层面）：
```
external_libs → database, utils*, module, flow → api
                  ↑___________|
                  utils 反向依赖 database 局部
```

\* utils 的 graph 子模块独立；geometry 和 visualization 反向依赖 database。

## 6.2 utils 是否依赖 module / flow / database？

| utils 子模块 | 依赖 database | 依赖 module | 依赖 flow |
|---|---|---|---|
| utils/graph | 无 | 无 | 无 |
| utils/logger | 无 | 无 | 无 |
| utils/geometry | **是**（icts_source_database_spatial; #include Point.hh/Rect.hh/Region.hh） | 无 | 无 |
| utils/visualization | **是**（PUBLIC icts_source_database_design + icts_source_database_spatial；#include "design/Pin.hh" "spatial/Tree.hh"） | 无 | 无 |

**结论**：
- utils 没有反向依赖 module 或 flow（命名空间隔离正确）。
- utils 的 geometry/visualization 反向依赖 database。问题在概念层："utils 应当是最底层"，但 Point / Rect / Region / Pin / Tree 这些类被放在 database/ 而不是 utils/，导致 utils 的几何/可视化必须借用 database。
- 修复方向（不实施）：把 spatial/Point.hh 等"通用类型"挪到 utils/spatial 或 utils/types；或者承认 utils ≮ database，改用"通用工具 vs 业务工具"两分。

## 6.3 database 是否依赖 flow / module？

- 扫描 `database/**/*.hh,*.cc` 的 include：**没有**`#include "flow/..."` 或 `#include "module/..."`。
- CMakeLists.txt 里 `icts_source_database` aggregator 仅依赖 `external_libs`、`utils`（间接通过子模块）。
- **结论**：database → flow/module 是干净的，无反向依赖。

## 6.4 module 是否依赖 flow？

- 扫描 `module/**/*.hh,*.cc` 的 include：**没有**`#include "flow/..."`。
- CMakeLists.txt 里 `module/topology/cluster_constraints/CMakeLists.txt:14` link 了 `icts_source_module_routing`（同层 module 之间 OK）。`flow/optimization/CMakeLists.txt:28` link 了 `icts_source_module_routing`（上层 link 下层 OK）。
- **结论**：module → flow 是干净的，无反向依赖。

## 6.5 .hh 互相 include 形成的依赖网

按"被本地 include 次数"排序 Top 20（仅本地 `#include "..."`）：

| 次数 | header | 角色 |
|---|---|---|
| 43 | `logger/Schema.hh` | utils/logger — 几乎所有 .cc 都包含 |
| 29 | `synthesis/htree/HTree.hh` | flow/synthesis/htree — H-tree 主接口 |
| 28 | `config/Config.hh` | database/config — 全局配置单例 |
| 21 | `design/Design.hh` | database/design — Design 单例 |
| 21 | `adapter/sta/STAAdapter.hh` | database/adapter/sta — STA 适配器 |
| 20 | `design/Pin.hh` | database/design |
| 20 | `design/Clock.hh` | database/design |
| 18 | `synthesis/htree/segment_pruning/SegmentLibrary.hh` | flow/synthesis/htree |
| 18 | `design/Inst.hh` | database/design |
| 17 | `design/Net.hh` | database/design |
| 16 | `io/Wrapper.hh` | database/io |
| 15 | `spatial/Point.hh` | database/spatial |
| 14 | `synthesis/htree/constraint/Constraint.hh` | flow/synthesis/htree |
| 13 | `design/ClockLayout.hh` | database/design |
| 12 | `synthesis/htree/topology_pruning/TopologyPruning.hh` | flow/synthesis/htree |
| 12 | `optimization/model/OptimizationTypes.hh` | flow/optimization |
| 11 | `synthesis/htree/compensation/RootDriverCompensation.hh` | flow/synthesis/htree |
| 11 | `geometry/Geometry.hh` | utils/geometry |
| 10 | `logger/LogFormat.hh` | utils/logger |
| 9 | `synthesis/topology/Topology.hh` | flow/synthesis/topology |

观察：
- 最热的是 `Schema.hh`（43）— logger 在所有模块流量都很高 → 是事实总线（同时也是单例耦合点）。
- `Config.hh` / `Design.hh` / `Wrapper.hh` / `STAAdapter.hh` 都是单例 → 单例不可避免地形成"扇入大头"。
- `HTree.hh` (29) 是 flow/synthesis/htree 的中心，被 H-tree 内的所有子模块包含。

## 6.6 是否有 .hh 互相 include 的循环？

抽查可疑路径（编译过 = 无 #include 循环，但可能有"双方需要前置声明"的耦合）：

- `database/design/`：Pin.hh 引用 Inst（class Inst forward declared），Inst.hh 引用 Pin（class Pin），ClockLayout.hh 引用 Pin/Net/Clock。**使用 forward declaration + .cc 内 include 模式**。OK。
- `flow/synthesis/htree/`：14 个子目录互相 include 频繁。例如 `synthesis/htree/HTree.hh` 包含 region/、segment_pruning/、topology_pruning/、constraint/、compensation/ 多个 hh — 是中心化结构而非环。
- `flow/optimization/`：所有 `OptimizationXxx.hh` 都用 `namespace icts::optimization_internal`，互相紧密耦合（OptimizationCandidates → OptimizationOptions → OptimizationTypes → OptimizationSolver → OptimizationState → OptimizationMutation → OptimizationReport → OptimizationPreparation），但 9 个文件之间的 include 是树状的，无环（待详细确认）。

**结论**：编译图层面无环（否则编译不过）；语义层面 `flow/synthesis/htree/` 和 `flow/optimization/` 内部耦合密集，是"团块"形态。

## 6.7 跨"应该独立"模块的耦合

- **`module/characterization/CharBuilder.hh` 引入 `adapter/fast_sta/FastStaTypes.hh`**（line 37）。这说明 module/characterization 的核心 header 公开依赖了 database/adapter/fast_sta 的类型 → 用户 PRD 第 4 条 "子模块内部设计不合理（fast_sta 案例）"的具体表现：
  - CharBuilder 把 FastSta 当作"特征化的内部组件"在用，类型从 fast_sta 流出到 module/characterization。
  - 这也使得任何 include CharBuilder.hh 的代码都隐式带入 fast_sta 类型。
- **`module/characterization/CharBuilderSlewSampling.cc:33` 和 `CharBuilderCircuit.cc:35` 直接 `#include "adapter/fast_sta/FastSta.hh"`** — CharBuilder 的 partial-impl 直接调用 `FAST_STA_INST`。这意味着 characterization 是 fast_sta 的实际驱动者，但二者都在 module/database/ 两层 — 调用方向：
  - characterization (module) → FastSta (database) ✓ 方向正常
  - 但 FastSta 本应是 STA backend 类型的"database"，结果被 module/characterization 这么紧密驱动，承担了不只是数据的职责 → fast_sta 实际是个"算法模块"被错放在 database/adapter/。

- **`flow/optimization/CMakeLists.txt:28` link `icts_source_module_routing`**：optimization 依赖 routing。表面合理（router 提供基础 routing），但优化器需要 routing 是值得记下的耦合（不一定是问题）。

## 6.8 概览图（简版）

```
utils/graph ─→ stdlib
utils/logger ─→ glog, ieda::Stats
utils/geometry ─→ database/spatial    ◀── 反向
utils/visualization ─→ database/design, database/spatial   ◀── 反向

database/spatial ─→ utils/(nothing)
database/design  ─→ database/spatial
database/config  ─→ external_libs
database/io      ─→ database/design, external_libs (idb)
database/adapter/sta ─→ external_libs (ista-engine)
database/adapter/fast_sta ─→ database/adapter/sta, database/design, ...
database/adapter/sdc ─→ external_libs (ista, idb)

module/topology  ─→ database/, utils/
module/routing   ─→ database/, utils/, salt, lemon
module/characterization ─→ database/adapter/fast_sta!!, database/adapter/sta, ...   ◀── 紧耦合到 fast_sta
module/timing    ─→ database/adapter/sta, ...

flow/setup       ─→ database/config/, database/io/
flow/evaluation/qor ─→ database/, module/
flow/synthesis/htree ─→ database/, module/, ...
flow/optimization ─→ module/routing!, flow/synthesis/, ...
flow/Flow.cc     ─→ everything

api/CTSAPI       ─→ flow/Flow, flow/setup, database/config, database/design,
                    database/io, flow/evaluation/qor, utils/logger,
                    feature_icts, feature_ista
```

## 6.9 关键发现汇总

1. **utils/{geometry, visualization} 反向依赖 database**（geometry → spatial、visualization → design+spatial）— 命名分层失效。
2. **module/characterization 紧耦合 database/adapter/fast_sta**（CharBuilder.hh 直接包含 FastStaTypes.hh）— 用户已点名的 fast_sta 设计问题。
3. **6 个单例 + 1 个 schema 单例形成事实"全局总线"**（CONFIG / DESIGN / WRAPPER / FLOW / STA_ADAPTER / FAST_STA + SCHEMA_WRITER），任何 .cc 可以直接调用，编译图查不出这些"语义"依赖。
4. **flow/optimization 用 module/routing** — 上层依赖下层（合规），但仍是高耦合点。
5. **`api → source` 单向干净**，但 source 内部子模块之间通过单例形成"看不见的全连接"。

## Caveats / Not Found

- 没用 graphviz 实际画图，仅根据 grep + CMake 推断。
- `flow/synthesis/htree/*` 14 个子目录之间的具体 include 图未细化。
- `module/topology/` 5 个子目录互相依赖未细化（kmeans/mcf/clustering/fast_clustering/cluster_constraints/config）。
