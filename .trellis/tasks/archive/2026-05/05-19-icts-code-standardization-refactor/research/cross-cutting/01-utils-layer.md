# Research: utils/ Layer Analysis

- **Query**: iCTS utils 层职责边界、是否纯通用工具、是否被正确使用、内部命名问题
- **Scope**: internal
- **Date**: 2026-05-19

## utils/ 总体结构

```
source/utils/
├── CMakeLists.txt                # aggregator: INTERFACE icts_source_utils
├── geometry/      (1 file)       # INTERFACE
├── graph/         (1 file)       # INTERFACE
├── logger/        (4 files)      # static lib
└── visualization/                # static lib + headers
    ├── cluster/   (2 files)
    ├── core/      (1 file)
    └── topology/  (2 files)
```

CMake：4 个子 target + 1 个聚合 INTERFACE。其中只有 `logger` 与 `visualization` 真正产生 .o；其余 `geometry`、`graph` 都是 header-only。

## 1.1 utils/geometry/

- 文件：`Geometry.hh` (267 行)
- 命名空间：`icts::geometry`
- 内容：模板化的 `Manhattan(Point, Point)`、`Manhattan(Point, Rect)`、`ProjectNearest(Region, Point)`、`CalcCenter`、`CalcMedian`、`ProjectToL1Circle`
- 依赖：`Point.hh` / `Rect.hh` / `Region.hh`（这三者都在 `database/spatial/`）
- CMake：`target_link_libraries(icts_source_utils_geometry INTERFACE icts_source_database_spatial)`

### 问题
- **utils 反向依赖 database/spatial**：geometry/ 用 database/spatial 的 Point/Rect/Region。命名上说 utils 是底层，但实际"几何计算"是 database 之上的 thin layer。spec 中"高内聚低耦合"角度看：要么把 Point/Rect/Region 视为 utils 类型并放进 utils/geometry，要么承认 geometry 不是"通用 utils"而是"database/spatial 的辅助函数"，应该并入 database/spatial 或叫 spatial/algorithms。
- 不能算 CTS 业务（只是几何算法），所以归在 utils 没毛病；但和 `database/spatial` 重叠严重 — `database/spatial/Point.hh` 也有 Manhattan-like 接口可能（待确认），有重复风险。
- `ProjectToL1Circle` 只对 `int` 特化，但 `CalcCenter`/`CalcMedian`/`Manhattan` 都是模板：API 风格不一致。

## 1.2 utils/graph/

- 文件：`RootedTreeLCA.hh` (245 行) — 单一类 `RootedTreeLCA`
- 命名空间：`icts::graph`
- 内容：rooted tree LCA 和 ancestor-path 帮助器
- 依赖：纯标准库；CMake 没有任何 `target_link_libraries`，只 `target_include_directories`

### 问题
- 真正的纯通用工具，最干净的一个子模块。没什么命名/依赖问题。
- 不过整个目录就一个文件、就一个类，CMake target 看起来"先占坑后扩"。

## 1.3 utils/logger/

- 文件：4 个 — `LogFormat.hh` (269 行), `Schema.hh` (222 行), `Schema.cc` (520 行), `SchemaScope.cc` (10K 字节, ~330 行)
- 命名空间：`icts::logformat`、`icts::schema`
- 内容：
  - LogFormat：纯字符串/表格 formatter（MakeTitle / FormatFixed / FormatEngineering / MakeTable / MakeKeyValueTable …）
  - Schema：单例 `SchemaWriter`（带 `getInst()` 和 `SCHEMA_WRITER_INST` 宏）；`StageScope`、`RuntimeMetricScope` 等 RAII；diagnose API、artifact emit API。
- CMake 链接：`PRIVATE log usage`（log 来自 src/utility/log，usage 来自 src/utility/usage 用作 ieda::Stats）
- 依赖：除标准库之外，`#include <glog/logging.h>`（在 .cc 内）

### 问题（命名/职责）
- `RuntimeMetricSnapshot` 是 SaaS / metrics 风味的命名（互联网化）。在 CTS 里更自然的名字：`RuntimeMetricSample` / `RuntimeMetricRecord`。
- 重度使用 SaaS 概念：`StageScope`、`ReportSink`(kDefault/kDetail/kBoth/kNone)、`DiagnosticLevel`、`emitArtifact` —— 这些都是观测平台/CI 的用语，不是 EDA 的"日志"惯例。EDA 里 logger 一般叫 "report"、"emit table"、"emit summary"。
- Schema 单例：`SchemaWriter::getInst()` + `SCHEMA_WRITER_INST` 宏。utils 层不该出现全局单例 — 这把日志状态变成了全局共享的可变态。如果未来要并行流程（多 clock domain），会冲突。
- Schema 既要写默认 sink 又要写 detail sink，并且支持嵌套 suspend/restore（_suspended_writers 栈）。这意味着 logger 不只是 logger，还是个"嵌套作用域的报告系统"。和"通用工具"已经有较大背离 — 这更接近 src/utility/report 风格。

### 是否被 module/flow 正确使用？
- 是的，43 次 `#include "logger/Schema.hh"`（最被频繁包含的本地 header）。基本所有 flow/* 和 module/* 都用 `SCHEMA_WRITER_INST.beginStage(...)`。命名规范："stage marker" / "stage scope" 已经成为事实标准。

## 1.4 utils/visualization/

- 子目录：`cluster/`、`core/`、`topology/`
- 文件：
  - `core/SvgCommon.hh` (323 行) — 常量、Bounds、SvgTransform、palette、ComputeBounds、MakeTransform、BuildClusterColors
  - `cluster/ClusterSvgWriter.hh/cc` (15K) — WriteClusterSvg / WriteClusterComparisonSvg
  - `topology/TopologySvgWriter.hh/cc` (5.7K) — WriteTopologySvg
- 命名空间：`icts::visualization::detail` (在 SvgCommon.hh)、`icts::visualization::cluster`、`icts::visualization::topology`
- CMake：`target_link_libraries(icts_source_utils_visualization PUBLIC icts_source_database_design icts_source_database_spatial)`

### 问题
- **utils 反向依赖 database**：visualization 直接 `#include "design/Pin.hh"` 和 `#include "spatial/Tree.hh"`。Pin、Tree 是 CTS 设计对象 —— 这意味着 visualization 严格说不是 "通用工具"，而是 "CTS-specific SVG writer"。
- core/ 下的 `SvgCommon.hh` 是真正的"可复用 SVG 辅助"（palette、Bounds、MapX/MapY），但里面也定义了 `ComputeLoadCentroid(std::vector<icts::Pin*>&)` —— 业务和通用混在一起。
- `cluster/` 和 `topology/` 的 SVG writer 用业务术语命名（ClusterSvgWriter / TopologySvgWriter），适合放在 module/topology 或 flow/report/visualization；放在 utils 是错位。
- 命名空间嵌套两层 `icts::visualization::cluster` 和 `icts::visualization::topology` —— 是合理设计但和其他 utils 子模块（icts::geometry、icts::graph 都是一层）风格不齐。

### 与 flow/report/visualization 的关系（重要发现）
- `flow/report/visualization/` 已经有完整的 visualization 子树（drawing/、gds/、svg/）。
- utils/visualization 是早期的"cluster/topology 调试用 SVG"，flow/report/visualization 才是正式输出。**职责重叠**：utils/visualization 应该并入 flow/report/visualization，或者两者明确分工（debug 用 / 正式输出用）。

## 1.5 utils 层综合问题

| 子模块 | 是真正通用工具吗 | 依赖了 CTS 业务吗 |
|---|---|---|
| geometry/ | 算法是通用的 | 依赖 database/spatial::Point/Rect/Region |
| graph/ | 是 | 否 |
| logger/ | 是 logging/reporting | 否（但和 flow/report 概念重叠） |
| visualization/ | 否 | 依赖 database/design::Pin、database/spatial::Tree |

- **utils 不是干净的"基础工具层"**：geometry 和 visualization 都依赖 database。CMake 里也明确写了：
  - `icts_source_utils_geometry INTERFACE icts_source_database_spatial`
  - `icts_source_utils_visualization PUBLIC icts_source_database_design icts_source_database_spatial`
- 层级倒挂：`icts_source_utils` 在 source/CMakeLists.txt 里和 database 平级（都被 icts_source 引用），但 utils 又 link database —— 拓扑上 utils 必须晚于 database 编译。这能编通是因为 utils 的子模块独立向 database 借用，没有完整 utils → database 引用，但概念上 utils 已经不"在 database 之下"了。
- 内部 Internal/Helper/Manager 等命名：utils 内部目前 **没有** `*Internal.hh`、`*Helper.hh`、`*Manager.hh`。命名层面 utils 是干净的（除 SchemaWriter 这个单例外）。

## Caveats / Not Found

- 没扫到 utils 内有 Snapshot/Internal/Support/Request/Response 命名（除 `RuntimeMetricSnapshot` 这个数据结构）。
- visualization/cluster 和 topology 的 .cc 文件没有读完整内容；如果里面有"私有数据复制 database 类型"的现象，本报告未覆盖。
- geometry/Geometry.hh 与 database/spatial/Point.hh 是否有 distance 重复函数，未确认（只看了 Geometry.hh）。
