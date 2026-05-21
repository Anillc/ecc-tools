# Research: CMake Structure Analysis

- **Query**: iCTS CMake target 总数、粒度、依赖关系、是否高内聚低耦合
- **Scope**: internal
- **Date**: 2026-05-19

## 3.1 顶层结构

`src/operation/iCTS/CMakeLists.txt` 加载顺序：
```
external_libs/  → source/  → api/  → test/
```

### external_libs/
```
external_libs/
├── CMakeLists.txt
├── icts_api_external_libs.cmake     → icts_api_external_libs INTERFACE: idm ista-engine log usage feature_db
├── icts_source_external_libs.cmake  → icts_source_external_libs INTERFACE: geometry_db idb IdbBuilder def_builder lef_builder def_service lef_service
└── icts_test_external_libs.cmake    → icts_test_external_libs INTERFACE: gtest pthread + 同 source 的 idb 链
```

3 个 INTERFACE library，仅打包外部依赖。无源码、纯转发链接。**详见 03 节"external_libs 分析"**。

### api/
- 1 个 library：`icts_api`（CTSAPI.cc 唯一一个源文件）。详见 [02-api-layer.md](02-api-layer.md)。

### source/ 总 target 数

| 类型 | 数量 |
|---|---|
| 简单单行 `add_library(name ...)` | 50 |
| 多行 `add_library(\n name\n src...)` | 38 |
| **总计 source library** | **88** |

加上 api（1）、external_libs（3） = 92 个 cmake target（不算 test 和 test/common helper 库）。

## 3.2 target 粒度评价

### 过细：单文件 / INTERFACE-only 仓库
列举 icts_source_* 中"只有 1 个 .cc"或纯 INTERFACE 的：

| Target | 文件 | 性质 |
|---|---|---|
| `icts_source_module_routing_cbs` | `CBSRouter.cc` | 1 个 cc |
| `icts_source_module_routing_flute` | `FLUTERouter.cc` | 1 个 cc |
| `icts_source_module_routing_salt` | `SALTRouter.cc` | 1 个 cc |
| `icts_source_module_routing_local_legalization` | `LocalLegalization.cc` | 1 个 cc |
| `icts_source_module_routing_database` | INTERFACE | 0 个 cc — 只是把 database/routing 改名转发 |
| `icts_source_module_topology_mcf/kmeans/config` | INTERFACE | 0 个 cc |
| `icts_source_database_qor/routing/timing/characterization/adapter/spatial/io/design/...` | 多数 INTERFACE | aggregator |
| `icts_source_flow_optimization_model` | INTERFACE | 0 个 cc — 只暴露 OptimizationTypes.hh |
| `icts_source_flow_synthesis_topology_buffer` | `BufferInsertion.cc` | 1 个 cc |
| `icts_source_flow_synthesis_trace_distance/domain_status/topology_result/layout` | 各 1 个 cc | |

→ **大约 30+ 个 target 只有 0~1 个 .cc**。

#### 问题
- CMake target 数量爆炸（88 个）但实际有"内容"的 target 远少于 88。
- 维护成本：每个 target 都要写 ~30 行 CMakeLists.txt（link_libraries / include_directories / DEBUG 选项）。88 × 30 = ~2640 行 CMake 模板代码。
- 编译并行度：CMake target 太碎不会显著提高并行度，因为依赖图深度大，仍要按层走。

### 过粗：单 target 文件数 >50
| Target | 文件数 | 评价 |
|---|---|---|
| 无 | — | 因为已经拆得很碎 |

最大的 target 也只有 ~15 个 .cc（fast_sta、bound_skew_tree）— 整体偏细而非偏粗。

### 应该拆分的 target（细但还粗）
| Target | 文件数 | 备注 |
|---|---|---|
| `icts_source_database_adapter_fast_sta` | 15 个 cc + 11 个 hh + Internal.hh | 已经偏大，并且涵盖 FastSta / FastStaTiming / FastStaPower / FastStaDmpCeff* / FastStaIncremental / FastStaParasitics / FastStaClockTree / FastStaLiberty / FastStaReport / FastStaTypes 多个职责。**实际是一个 mini library，应该按"内核 vs DMP-Ceff vs Power vs Liberty"拆**。用户 PRD 已经特别提到 fast_sta。 |
| `icts_source_module_routing_bst` | 14 个 cc | 把 GeomCalc(+3 个 cc)、Components、BSTRouter(+2)、BoundSkewTree(+5)、BSTRouterBinaryTopology 全都塞进一个 target。GeomCalc/Components 是几何工具，BoundSkewTree 是算法，BSTRouter 是包装 —— 至少 3 个职责。 |
| `icts_source_module_characterization` | 13 个 cc + 8 个 hh | 12 个 `CharBuilder*.cc`（PatternEnumeration / PatternStorage / SampleStorage / Sampling / SlewSampling / StaSampling / Topology / Build / Circuit / Config / Feasibility / 主 CharBuilder.cc）。粒度上是 sub-stage 拆分，本来还算 OK，但 12 个文件共享 1 个 `CharBuilder` 类（friend-like）—— 实际是 partial-implementation pattern。 |

## 3.3 target 间依赖关系

### 高层链路（按层组装）
```
icts_source (INTERFACE)
  ├── icts_source_database (INTERFACE)
  │    ├── icts_source_database_adapter (INTERFACE)
  │    │    ├── icts_source_database_adapter_fast_sta
  │    │    ├── icts_source_database_adapter_sdc
  │    │    └── icts_source_database_adapter_sta
  │    ├── icts_source_database_config       (1 cc)
  │    ├── icts_source_database_design       (多 cc，未读 CMake)
  │    ├── icts_source_database_io           (多 cc)
  │    ├── icts_source_database_spatial      (INTERFACE)
  │    ├── icts_source_database_routing      (INTERFACE)
  │    ├── icts_source_database_timing       (INTERFACE)
  │    ├── icts_source_database_characterization (INTERFACE)
  │    └── icts_source_database_qor          (INTERFACE)
  ├── icts_source_utils
  │    ├── icts_source_utils_geometry        (INTERFACE → icts_source_database_spatial!)
  │    ├── icts_source_utils_graph           (INTERFACE)
  │    ├── icts_source_utils_logger          (static, PRIVATE log usage)
  │    └── icts_source_utils_visualization   (static, PUBLIC icts_source_database_design icts_source_database_spatial!)
  ├── icts_source_module
  │    ├── icts_source_module_routing (10+ sub-targets)
  │    ├── icts_source_module_timing
  │    ├── icts_source_module_topology (4 sub-targets)
  │    ├── icts_source_module_characterization
  │    └── icts_source_module_analytical_characterization
  └── icts_source_flow
       ├── icts_source_flow_evaluation
       ├── icts_source_flow_instantiation
       ├── icts_source_flow_optimization
       ├── icts_source_flow_report
       ├── icts_source_flow_setup
       └── icts_source_flow_synthesis
```

### 层序违反
1. **utils 反向依赖 database**（已在 01-utils-layer.md 详述）：
   - `icts_source_utils_geometry INTERFACE icts_source_database_spatial`
   - `icts_source_utils_visualization PUBLIC icts_source_database_design icts_source_database_spatial`
   命名上 utils 应该在 database 之下，但实际反过来。
2. **module_routing 借 database** —— 这是正常的，module 在 database 之上。
3. **flow 借 module** —— 正常。
4. **api 借 source** —— 正常。

### 无明显循环（CMake 编译能通过）
- 没扫到 `flow → module → flow` 或 `database → module` 之类的反向。
- utils → database 这条"反向"是 CMake 允许的（utils 子模块只 link 自己需要的 database 子模块，而 icts_source_utils 是 INTERFACE aggregator，不强制顺序）。

### 缺失的 module/routing 子聚合
- `module/routing/` 目录下有 8 个 subdir：`router/database/helper/flute/salt/bound_skew_tree/concurrent_bst_salt/local_legalization`，但 `module/routing/CMakeLists.txt` 只 `add_subdirectory`，没建一个 `icts_source_module_routing` aggregator。
- 真正的 `icts_source_module_routing` target 定义在 `module/routing/router/CMakeLists.txt:1-4` —— 它就是 Router.cc，但同时 link 了所有 routing/* 子目录！这意味着 **`icts_source_module_routing` 名字是 router/，但实际打包了整个 routing/**。命名误导。
- 修复方向（**这是用户最初提到的"flow 层缺统一范式 + module/flow 子模块层次/命名不统一"的具体实例**）：要么把 `router/` 升级为 `module/routing/` 的 aggregator，要么把 Router.cc 移到 `module/routing/` 顶层直接定义 aggregator。

### 反复定义 DEBUG_ICTS_xxx 编译选项
顶层 CMakeLists.txt 列出 10+ 个 `option(DEBUG_ICTS_xxx ...)`：
- DEBUG_ICTS_SOURCE_DATABASE_CONFIG / DESIGN / IO / SPATIAL / DATABASE
- DEBUG_ICTS_SOURCE_UTILS_LOGGER / GEOMETRY / UTILS
- DEBUG_ICTS_SOURCE_FLOW
- DEBUG_ICTS_SOURCE_MODULE_TOPOLOGY_KMEANS / MCF / CLUSTERING / TOPOLOGY / MODULE
- DEBUG_ICTS_SOURCE / API / TEST

但**单个 CMakeLists.txt 文件内**几乎都有重复的 `if(DEBUG_ICTS_xxx)` 块加 `-g3 -O0 -fno-inline -fno-omit-frame-pointer`。一共 88 个 target × 10 行 = ~880 行重复 CMake。

## 3.4 高内聚低耦合评分

| 维度 | 评分 | 备注 |
|---|---|---|
| target 数量合理性 | 偏多 | 88 个 target，30+ 个是单文件或 INTERFACE。 |
| 命名规范性 | 中等 | `icts_source_xxx_yyy` 一致；但 `icts_source_module_routing` 是 router/ 单文件，又同时聚合整个 routing/。 |
| 依赖方向 | 总体一致 | utils → database 反向是唯一突出问题 |
| 循环依赖 | 无 | 没发现 |
| DEBUG 编译选项 | 模板重复 | 应抽出公共函数 `icts_apply_debug_flags(target_name)` |
| 子目录范式 | 不统一 | `module/routing/` 缺 aggregator，子目录里有的 INTERFACE 有的 static lib |

## 3.5 建议（不写代码）
- 这部分本来不是 research agent 的工作，但用户问题里要求"target 间的依赖关系是否清晰、无环、是否符合高内聚低耦合"。明确发现：
  - utils → database 反向（geometry 和 visualization 都依赖 database）
  - module/routing 缺 aggregator
  - DEBUG_ICTS_xxx 模板重复
  - 30+ 个过细 target

## Caveats / Not Found

- 没列出 `icts_source_database_design` 和 `icts_source_database_io` 内部具体 cc，因为这是另一位调研专员的范围。
- 多个 INTERFACE 库的转发链没追到底（如 `icts_source_module_routing_database` → `icts_source_database_routing` → ...）。
- test/ 下的 CMakeLists 没全部展开（test/common/ 下还有 8 个子目录的 helper 库）。
