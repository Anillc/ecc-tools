# Research: database 子模块职责梳理

- **Query**: 梳理 `source/database/` 下 8 个子目录的职责、顶层入口、可能的重复/冗余
- **Scope**: internal
- **Date**: 2026-05-19

---

## 总览

`src/operation/iCTS/source/database/` 顶层 8 个子目录（不计 `adapter/`，单独详述）：

```
database/
├── adapter/         (见 01/02 文档，最大子模块)
├── characterization/  特征化数据模型
├── config/            配置存储
├── design/            设计数据
├── io/                I/O 与外部 iDB 桥接
├── qor/               质量度量
├── routing/           路由数据结构
├── spatial/           几何基础类型
└── timing/            时序数据结构
```

CMake 层级（`database/CMakeLists.txt:22-36`）：`icts_source_database` 为 INTERFACE 库，把上述 9 个子模块作为接口聚合。

---

## 1. `adapter/` 子模块

| 子目录 | 文件数 | 总行 | 角色 |
|---|---|---|---|
| `adapter/fast_sta/` | 28 | 4949 | CTS 自有快速 STA 引擎（最重，详见 01-fast_sta-deep-dive.md） |
| `adapter/sta/` | 12 | ~5500 | iSTA 适配层 |
| `adapter/sdc/` | 15 | ~7000 | SDC 解析与 clock trace |

### 1.1 `adapter/sta/`

顶层入口：`STAAdapter.hh`（`class STAAdapter` 单例，`STA_ADAPTER_INST` 宏）

公共 API 数：30+ 个静态方法（行 99-130），涵盖：
- Liberty 查询（`queryCellOutPinCapLimit`、`queryCellInPinSlewLimit`、`queryCellAreaUm2`、`queryCharInputPinCap`）
- Pin/Net 查询（`queryWireResistance`、`queryWireCapacitance`、`queryPinSlew`、`queryPinClockArrival`）
- Inst 类型（`queryInstType`、`isFlipFlop`）
- 时序流程（`init`、`setPropagatedClocks`、`updateTiming`、`reportTiming`、`refreshFullDesignTimingContext`）
- Clock metrics（`queryClockTiming`、`queryClockTimings`、`queryClockLatencySkew`）
- RC tree 安装（`installClockNetRcTree`）
- root driver 查询（`queryRootDriverCostDirect`、`queryBufferPorts`）
- 报告（`emitUnitWireRcReport`、`emitConfiguredUnitWireRcReport`）

内部头：`STAAdapterInternal.hh` (96 行) 暴露 `namespace sta_adapter_internal` 给 6 个实现 .cc 共享 `WireRcProbe` struct + 25 个自由函数。

文件拆分（`CMakeLists.txt:1-11`）：
- `STAAdapter.cc` (50 行)
- `STAAdapterCellQuery.cc` (16031 字节)
- `STAAdapterClockLookup.cc` (4362 字节)
- `STAAdapterInternal.cc` (20271 字节)
- `STAAdapterRcTree.cc` (7419 字节)
- `STAAdapterRootDriverQuery.cc` (10584 字节)
- `STAAdapterTimingUpdate.cc` (15628 字节)
- `STAAdapterWireRc.cc` (3987 字节)

**重叠 / 冗余分析**：
- `STAAdapter::queryWireResistance` vs `FastStaParasitics::queryWireResistanceOhm`（fast_sta 调 STAAdapter）—— 单向依赖，合理
- `STAAdapter::queryCellOutPinCapLimit` 与 `STAAdapter::queryCellOutPinCapTableAxisMax` —— 两个相近 API，文档区分不清
- 无概念重复，但 `STAAdapterInternal` 函数粒度过细（25 个自由函数），其中`Convert*` 单位换算可独立为 `units/` 子目录

### 1.2 `adapter/sdc/`

顶层入口：
- `ClockTraceResolver.hh`（`class ClockTraceResolver`，2 个静态方法，含 `ClockTraceResult` / `ClockTraceRecord` POD）
- `SdcClockModel.hh`（5 个 POD：`SdcObjectKind` / `SdcObjectRef` / `SdcClockDecl` / `SdcCaseAnalysis` / `SdcClockData`）
- `SdcClockReader.hh`（`class SdcClockReader`，2 个实例方法，从 .sdc 文件读取）
- `SdcClockParser.hh`（**注意：注释行 21 说 "Internal SDC clock subset parser declarations"**，但实际在公共目录）

内部头：`ClockTraceResolverInternal.hh` (155 行)，`namespace icts::clock_trace`，含 50+ 自由函数、9 个 struct。

文件拆分（`CMakeLists.txt:1-12`）：
- `ClockTraceResolver.cc` (4764 字节) + `ClockTraceResolve.cc` (9436 字节) + `ClockTracePins.cc` (18958 字节) + `ClockTraceReport.cc` (9127 字节)
- `SdcClockReader.cc` (3409 字节)
- `SdcClockParser.cc` (9312 字节)
- `SdcClockEvaluator.cc` (7896 字节)
- `SdcClockCommands.cc` (11065 字节)
- `SdcClockValue.cc` (10474 字节)

**与 fast_sta 概念重叠**：
- `SdcClockData` (`SdcClockModel.hh:74`) vs `FastStaClockContext` (`FastStaTypes.hh:333`) —— 不重叠：前者是 SDC declarations，后者是运行时 STA 状态
- `ClockTraceResolver` 与 `FastStaBuilder` —— 都"构造"clock 相关数据，但前者读 SDC.sdc → 输出 net-pair 提示给 Wrapper，后者读 `design::Clock` → 输出 STA-ready context，**不冲突但都叫 "build"，易混淆**

**职责评估**：sdc 子目录混合了 4 件事：
1. SDC 文件 lexer/parser（`SdcClockParser`、`SdcClockEvaluator`、`SdcClockValue`、`SdcClockCommands`）
2. SDC 数据模型（`SdcClockModel`）
3. SDC 文件读取入口（`SdcClockReader`）
4. SDC → iDB 网解析（`ClockTraceResolver*` 4 个文件）

建议拆为 `adapter/sdc/parser/` + `adapter/sdc/clock_trace/` 两个子目录。

---

## 2. `characterization/` 子模块

文件清单（全部仅有 .hh）：

| 文件 | 用途 |
|---|---|
| `BufferingPattern.hh` (172 行) | buffer 链 pattern 描述 + 拼接 |
| `CharCore.hh` (90 行) | 特性化核心数据 |
| `HTreeTopologyChar.hh` (170 行) | H-tree 拓扑特性化数据 |
| `HTreeTopologyPattern.hh` (107 行) | H-tree pattern 模板 |
| `PatternId.hh` (82 行) | pattern 标识符 |
| `SegmentChar.hh` (138 行) | 单 segment 特性化数据 |
| `ValueLattice.hh` (99 行) | wirelength 离散格点 |

**特点**：纯 .hh 库，无 .cc。`CMakeLists.txt` 仅 16 行，应为 INTERFACE 库。所有类型都是 POD 或 template，header-only 设计合理。

**关注点**：
- 与 fast_sta 的 `FastStaCharTopologySpec` / `FastStaCharSampleResult` （在 `FastStaTypes.hh:355-379`）有概念相关性但无冲突。后者是"采样请求/结果"，前者是"采样结果存储/合成"
- characterization 是 module 层（`source/module/characterization/`）的数据底座，不应反向依赖 module

---

## 3. `config/` 子模块

仅 1 个类：`config/Config.hh` (208 行) + `Config.cc` (14586 字节)

`class Config` 单例（`CONFIG_INST` 宏），含 30+ getter/setter，分两组：
- 算法参数：`_skew_bound`、`_max_buf_tran`、`_root_input_slew`、`_max_sink_tran`、`_max_cap`、`_max_fanout`、`_routing_layers`、`_buffer_types`、`_wirelength_unit_um`、`_wirelength_iterations`、`_slew_steps`、`_cap_steps`、`_wire_width`、`_char_buf_redundancy_pct`、`_force_branch_buffer`、`_htree_depth_explore_window`、`_htree_topology_tolerance`、`_enable_analytical_htree`、`_enable_sink_clustering`
- 文件路径：`_work_dir`、`_log_file`、`_visualization_dir`、`_statistics_dir`、`_use_netlist`、`_net_list`、`_last_error`

**评估**：
- 用 30+ 个 setter/getter 的大对象，没有按子领域分组
- 文件路径与算法参数混在同一 `Config` 单例，违反单一职责
- 建议拆为 `Config::Algorithm` + `Config::Paths` + `Config::Optimization` 子结构

---

## 4. `design/` 子模块

| 文件 | 行数 | 职责 |
|---|---|---|
| `Design.hh` | 109 | `Design` 单例（`DESIGN_INST` 宏），聚合所有 Clock/Inst/Pin/Net 持有 |
| `Clock.hh` | 95 | `Clock` 类——单时钟的输入与最终成员 |
| `ClockDAG.hh` | 107 | `ClockDAG`——committed CTS 拓扑的只读 DAG 投影 |
| `ClockLayout.hh` | 172 | `ClockLayout`——只读 layout 投影（含枚举与 segment/net/inst） |
| `ClockNetwork.hh` | 152 | `ClockNetwork`——稳定的语义模型（域、根 buffer、拓扑记录） |
| `Inst.hh` | 116 | `Inst` 类 |
| `Net.hh` | 56 | `Net` 类 |
| `Pin.hh` | 73 | `Pin` 类 |

**疑点：四个 Clock* 类的边界**

- `Clock`：输入端的 sdc decl + 成员（loads/insts/nets）
- `ClockDAG`：DAG 视图（pin-net arc 拓扑）
- `ClockLayout`：geometry / layout 视图
- `ClockNetwork`：语义视图（域、buffer roles、topology records）

**冗余风险**：
- `ClockLayout::SinkDomainKind` (`ClockLayout.hh:64-70`) vs `ClockNetwork::DomainKind` (`ClockNetwork.hh:42-46`) —— 两个枚举几乎一样
- `ClockLayout::LayoutInstRole` vs `ClockNetwork::InstRole` —— 两套 buffer 角色枚举
- `ClockLayout::LayoutNetRole` vs `ClockNetwork::NetRole` —— 两套 net 角色枚举
- `ClockLayout::ClockLayoutPhase` 与 `ClockNetwork` 中无对应，单边
- 都有 `ClockLayoutSegment`、`ClockLayoutNet`、`ClockLayoutInst`、`ClockLayoutClock` 4 个 struct vs `ClockNetwork::DomainRecord`、`InstRecord`、`NetRecord` 3 个 struct

证据：两套近似枚举共存，且各有 `ToString()` 输出（`ClockLayout.hh:165-169`）。

### 与 module/flow 层的边界

`Clock` 是 CTS 表面对象；`ClockDAG`/`ClockLayout`/`ClockNetwork` 是 CTS 内部计算结果的不同投影。`flow/` 层会消费这些投影。建议确认：
- `ClockDAG` vs `ClockNetwork` —— 是否真的需要两个？DAG 是结构，Network 是语义；二者可合并为 `ClockNetwork` 含 `Arc` / `Graph` 字段

---

## 5. `io/` 子模块

文件清单（`io/CMakeLists.txt:1-7`，4 cc + 2 hh + 1 internal hh）：

| 文件 | 行 | 职责 |
|---|---|---|
| `Wrapper.hh` | 172 | `class Wrapper` 单例 + `WrapperCellGeometry` + `WrapperWriteResult` |
| `Wrapper.cc` | (6059 字节) | init / queryDbUnit / withinCore / read / idbToCts / ctsToIdb / collectLogicCellGeometries |
| `WrapperClockReader.cc` | (14156 字节) | `Wrapper::CtsClockReader` 嵌套类——iDB → CTS Clock 拷贝 |
| `WrapperClockWriter.cc` | (18560 字节) | `Wrapper::CtsClockIdbWriter` 嵌套类——CTS Clock → iDB 写回 |
| `WrapperClockWriterInternal.hh` | 67 | 3 个 struct + 4 个自由函数（snapshot / append / clear / find pins） |
| `WrapperClockWriterSupport.cc` | (3475 字节) | 实现上述 4 个函数 |

**职责评估**：
- `Wrapper` 包了 iDB（IdbBuilder / IdbDesign / IdbLayout）和 SDC 转换桥接（`traceSdcClocks`）
- 命名"Wrapper"是互联网化，应为 `IdbBridge` / `CtsIdbAccess`
- 整个文件夹用 friend 内嵌类 `CtsClockReader` / `CtsClockIdbWriter` 实现拆分，**这是合理的封装方式**（vs `Internal.hh` 共享 namespace）

---

## 6. `qor/` 子模块

仅 1 文件：`Qor.hh` (63 行)，4 个 POD：`QorCellStats`、`QorLibCellDistribution`、`Qor`。无 .cc。

**评估**：纯数据载体，header-only 合理。

---

## 7. `routing/` 子模块

| 文件 | 行 | 职责 |
|---|---|---|
| `RoutingTerminal.hh` | 45 | `RoutingTerminal` / `ClockRoutingTerminal` 终端 POD |
| `SteinerTree.hh` | 349 | `SteinerNode` / `SteinerEdge` / `SteinerTree` 模板 + `ClockSteinerNode` / `ClockSteinerEdge` / `ClockSteinerTree` 派生 |

无 .cc。所有类型都是模板。

**评估**：
- 命名清晰（无 Wrapper/Helper）
- 与 `spatial/Tree.hh` 有结构相似性（节点-边-根），但分工不同：spatial 树是位置树（含 `Point<int>`），routing 树是 Steiner 树（含 `distance` 与 `routed_distance`）

---

## 8. `spatial/` 子模块

| 文件 | 行 | 职责 |
|---|---|---|
| `Point.hh` | 124 | `Point<T>` 模板 |
| `Rect.hh` | 99 | `Rect<T>` 模板 |
| `Region.hh` | 190 | `Region<T>` 模板（矩形并集 + subtract + project_nearest） |
| `Tree.hh` | 182 | `TreeNode` / `Tree` 拓扑树类 |

无 .cc。

**评估**：
- `Tree.hh` 的 `TreeNode` 持有 `_loads`（`std::vector<Pin*>`），引入了对 `design/Pin` 的引用 —— spatial 不应依赖 design
  - 证据：`spatial/Tree.hh:35` `class Pin;` + 行 50-51 `get_loads()` 返回 `std::vector<Pin*>&`
  - 这违反了 "spatial 是基础几何，无业务依赖" 的预期
- `Region.hh` 实现 rectilinear region 的 add/subtract/merge，逻辑复杂（190 行算法），考虑独立成 .cc
- `Region.hh` 与 `Rect.hh` 已分开，OK

---

## 9. `timing/` 子模块

仅 1 文件：`RCTree.hh` (292 行) —— `RCTreeVertex` / `RCTreeArc` / `class RCTree` 实现。

无 .cc。

**评估**：
- 与 fast_sta 的 `FastStaNetParasitic` 概念重叠：都是 RC 树
- 区别：`RCTree` 通用且独立，`FastStaNetParasitic` 嵌入 `FastStaNet` 内
- 实际依赖？检查 grep（`RCTree` 在 source/ 中只有 timing/RCTree.hh 自己引用）—— **该文件目前没有使用方**，可能是历史遗留或未启用的数据结构

---

## 子模块间数据流总览（database 内）

```
adapter/sdc/  ───┐
                 │
adapter/sta/  ───┤   读取/转换
                 │
io/Wrapper    ───┴───────────────► design/Clock/Inst/Pin/Net  (Design 单例)
                                          │
                                          ├──► design/ClockDAG       (committed 拓扑)
                                          ├──► design/ClockNetwork   (语义模型)
                                          └──► design/ClockLayout    (layout 投影)
                                                       │
                                                       ▼
                       adapter/fast_sta  ──────► FastStaClockContext   (STA-ready)
                                                       │
                       (flow/optimization 消费)         ▼
                                                  Skew / Power / Slew metrics
```

`spatial/` 与 `routing/` 是基础类型库，被多处使用。`characterization/` 是 module 层用。`qor/` 是 flow 层输出。`timing/RCTree` 当前无主消费者。

---

## 关键发现

1. **design/ 内部 4 个 Clock* 类有大量重叠枚举** —— `ClockLayout` 与 `ClockNetwork` 各持一套 DomainKind/InstRole/NetRole，几乎等价
2. **spatial/Tree.hh 反向依赖 design/Pin** —— 违反分层
3. **timing/RCTree.hh 似乎未被任何 src 使用**
4. **adapter/sdc/ 内部混合 4 件事**：SDC 解析、SDC 模型、SDC 读取入口、SDC 走线 —— 可拆为 `parser/` + `clock_trace/`
5. **adapter/sta/ 内部 25 个工具函数堆在 STAAdapterInternal**，可按职能拆为 `units/` `liberty/` `engine/` 子目录
6. **io/Wrapper 命名 = 互联网化**，应改 `IdbBridge`
7. **Config 单例 30+ getter/setter，无分组**
8. **fast_sta/FastStaTypes.hh 单文件 24 个 struct/6 enum**，可拆 5 个子主题头
