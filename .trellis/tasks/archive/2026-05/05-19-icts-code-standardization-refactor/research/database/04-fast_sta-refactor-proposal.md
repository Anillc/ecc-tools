# Research: fast_sta 拆分重构方案

- **Query**: 给出 fast_sta/ 按高内聚低耦合的拆分方案（子目录 + cmake target）
- **Scope**: internal (`src/operation/iCTS/source/database/adapter/fast_sta/`)
- **Date**: 2026-05-19

---

## 设计原则

1. **唯一对外接口** = `FastSta.hh`/`FastSta.cc` + `FastStaTypes.hh`（类型不可避免被 flow 层使用）
2. **子模块** = 自洽的目录 + 自己的 CMake target + 子目录内可见的 internal 头
3. **CMake target_include_directories**:
   - `PUBLIC` 仅暴露顶层 `fast_sta/` 目录给外部
   - 子目录头通过 `PRIVATE` 仅在子目录 cc 中可见，或仅子模块自身链接者可见
4. **消灭 Internal.hh**：通过子目录隔离实现物理边界，不再依赖文件名约定

---

## 推荐子目录结构

```
adapter/fast_sta/
├── CMakeLists.txt                     # 聚合 INTERFACE 库 icts_source_database_adapter_fast_sta
│
├── FastSta.hh                         # 唯一对外门面接口
├── FastSta.cc                         # 单例 + 17 个公共 API（删除 7 个未用 API）
│
├── types/                             # 子模块 1：共享数据类型
│   ├── CMakeLists.txt                 #   icts_source_database_adapter_fast_sta_types (INTERFACE)
│   ├── FastStaIds.hh                  #   FastStaClockId / FastStaNodeId / FastStaNetId / FastStaRcNodeId / FastStaCharContextId + kInvalid*
│   ├── FastStaEnums.hh                #   FastStaNodeKind / FastStaTransition / FastStaSlewKind / FastStaDmpAlgorithm / FastStaLibertyTableKind
│   ├── FastStaGeometry.hh             #   FastStaPoint / FastStaPointKeyHash / FastStaRcSegment
│   ├── FastStaLibertyTypes.hh         #   FastStaLibertyAxis / FastStaLibertyTable + lookup() / FastStaLibertyArc / FastStaLibertyCell
│   ├── FastStaParasiticTypes.hh       #   FastStaPiModel / FastStaRcNode / FastStaRcEdge / FastStaNetParasitic
│   ├── FastStaTimingTypes.hh          #   FastStaTimingPoint / FastStaDmpDriverResult / FastStaDmpLoadResult
│   ├── FastStaPowerTypes.hh           #   FastStaPowerSummary
│   ├── FastStaNodeNet.hh              #   FastStaNode / FastStaNet
│   ├── FastStaContext.hh              #   FastStaClockContext
│   ├── FastStaIncrementalTypes.hh     #   FastStaDirtyRegion / FastStaBufferMasterChange
│   ├── FastStaStatusTypes.hh          #   FastStaCapStatus / FastStaSlewStatus / FastStaSkewSummary
│   ├── FastStaCharTypes.hh            #   FastStaCharContextId / FastStaCharSegmentSpec / FastStaCharTopologySpec / FastStaCharSampleResult
│   └── FastStaTypes.cc                #   FastStaLibertyTable::lookup() 实现
│
├── liberty/                           # 子模块 2：从 iSTA Liberty 提取 cell 表
│   ├── CMakeLists.txt                 #   icts_source_database_adapter_fast_sta_liberty
│   ├── FastStaLiberty.hh              #   公开：extractBufferCell()
│   ├── FastStaLiberty.cc              #
│   └── FastStaLibertyInternal.hh      #   子目录内部：percentOrDefault / extractTable / findBestTimingArc
│                                      #   （Internal 命名仅在子目录内部可见，不暴露）
│
├── parasitics/                        # 子模块 3：RC 段/路由树 → Pi+Elmore reduction
│   ├── CMakeLists.txt                 #   icts_source_database_adapter_fast_sta_parasitics
│   ├── FastStaParasitics.hh           #   公开：updateNetLoads / buildNetParasiticFromSegments / buildNetParasiticFromRouteTree
│   ├── FastStaParasitics.cc           #   主入口
│   ├── FastStaParasiticsReduction.cc  #   reduceToPiElmore + 矩量法
│   └── FastStaParasiticsWireQuery.cc  #   queryWireResistanceOhm/Cap 单位查询（依赖 STAAdapter）
│
├── dmp_ceff/                          # 子模块 4：DMP effective-capacitance 求解器
│   ├── CMakeLists.txt                 #   icts_source_database_adapter_fast_sta_dmp_ceff
│   ├── FastStaDmpCeff.hh              #   公开：calcDriverTiming / calcLoadDelaySlew / calcInputPortDelaySlew
│   ├── FastStaDmpCeff.cc              #   公开方法实现 + 负载侧波形 vl/vl0/findVlCrossing
│   ├── DmpSolver.hh                   #   私有：DmpSolver 类（取代 FastStaDmpCeffInternal.hh）
│   ├── DmpSolver.cc                   #   私有：构造 + solve + solveCap/Pi/ZeroC2 + initPi/ZeroC2
│   ├── DmpSolverEquations.cc          #   私有：y/dy/evalPi/evalOnePole/newtonRaphson/lu*
│   ├── DmpLibertyLookup.hh            #   私有：OutputThreshold/InputThreshold/SlewDerate/GateDelaySlew/GateModelRdNsPerPf
│   ├── DmpLibertyLookup.cc            #
│   ├── DmpNumerics.hh                 #   私有：DmpExp / FindRoot 数值工具
│   └── DmpNumerics.cc                 #
│
├── clock_tree/                        # 子模块 5：从 design::Clock 构建拓扑
│   ├── CMakeLists.txt                 #   icts_source_database_adapter_fast_sta_clock_tree
│   ├── FastStaClockTree.hh            #   公开：buildFromClock / buildFromClockLayout / applyLayoutParasitics
│   └── FastStaClockTree.cc            #
│
├── builder/                           # 子模块 6：协调初始化（liberty + clock_tree + parasitics + char）
│   ├── CMakeLists.txt                 #   icts_source_database_adapter_fast_sta_builder
│   ├── FastStaBuilder.hh              #   公开：buildClockContext × 2 + injectNetRouteTree
│   ├── FastStaBuilder.cc              #   协调入口
│   ├── FastStaCharBuilder.hh          #   公开：buildCharContext / setCharLoad / runCharSample
│   └── FastStaCharBuilder.cc          #
│
├── incremental/                       # 子模块 7：增量改 buffer master + 标脏域
│   ├── CMakeLists.txt                 #   icts_source_database_adapter_fast_sta_incremental
│   ├── FastStaIncremental.hh          #
│   └── FastStaIncremental.cc          #
│
├── timing/                            # 子模块 8：拓扑序时序传播
│   ├── CMakeLists.txt                 #   icts_source_database_adapter_fast_sta_timing
│   ├── FastStaTiming.hh               #
│   └── FastStaTiming.cc               #
│
├── power/                             # 子模块 9：功耗求和
│   ├── CMakeLists.txt                 #   icts_source_database_adapter_fast_sta_power
│   ├── FastStaPower.hh                #
│   └── FastStaPower.cc                #
│
└── report/                            # 子模块 10：日志摘要
    ├── CMakeLists.txt                 #   icts_source_database_adapter_fast_sta_report
    ├── FastStaReport.hh               #
    └── FastStaReport.cc               #
```

---

## CMake Target 依赖图

```
icts_source_database_adapter_fast_sta (INTERFACE/STATIC, 顶层聚合)
├── icts_source_database_adapter_fast_sta_types (INTERFACE, header-only POD)
│
├── icts_source_database_adapter_fast_sta_liberty
│   └── types
│
├── icts_source_database_adapter_fast_sta_parasitics
│   ├── types
│   └── icts_source_database_adapter_sta  (外部依赖：wire RC 单位查询)
│
├── icts_source_database_adapter_fast_sta_dmp_ceff
│   └── types
│
├── icts_source_database_adapter_fast_sta_clock_tree
│   ├── types
│   ├── parasitics
│   ├── icts_source_database_design
│   └── icts_source_database_spatial
│
├── icts_source_database_adapter_fast_sta_builder
│   ├── types
│   ├── liberty
│   ├── parasitics
│   ├── clock_tree
│   ├── timing  (调 FastStaTiming::update)
│   ├── power   (调 FastStaPower::update)
│   ├── icts_source_database_design
│   ├── icts_source_database_io  (Wrapper for DBU)
│   ├── icts_source_database_config
│   └── icts_source_database_adapter_sta (STAAdapter for liberty)
│
├── icts_source_database_adapter_fast_sta_incremental
│   ├── types
│   └── liberty
│
├── icts_source_database_adapter_fast_sta_timing
│   ├── types
│   ├── parasitics
│   └── dmp_ceff
│
├── icts_source_database_adapter_fast_sta_power
│   └── types
│
└── icts_source_database_adapter_fast_sta_report
    └── types
```

顶层 `FastSta.cc` 链接所有子目标。

---

## 关键设计决策

### 1. `types/` 是 header-only 的 INTERFACE 库

所有 POD struct 与 enum 都暴露给所有子模块（types 是公共契约）。

- **PUBLIC include**：`${ICTS_DATABASE_ADAPTER_FAST_STA}/types`
- 跨子模块共享类型是必要的（如 `FastStaClockContext` 被 builder/timing/power/incremental 都使用）

### 2. `dmp_ceff/` 子目录内的 `DmpSolver.hh` 不再叫 "Internal"

- 文件改名为 `DmpSolver.hh` —— 描述对象，不描述可见性
- 该 .hh 只通过 `${ICTS_DATABASE_ADAPTER_FAST_STA_DMP_CEFF}` 的 `PRIVATE` include 暴露
- 外部子模块（如 timing/）调用 `FastStaDmpCeff.hh` 公开接口，看不到 `DmpSolver`

### 3. `builder/` 单独成子目录

`FastStaBuilder` 协调初始化的 3 步（ClockTree → Liberty → Parasitics）+ char context 构造，且需要 STAAdapter / Config / Wrapper —— 是依赖最多的"组合根"，单独隔离避免污染其他子模块。

### 4. `FastSta.cc` 的"日志包装"压缩

`FastSta.cc` 当前 389 行有大量日志计时代码（`elapsedSeconds` / `logContextSize`）。可抽出 `FastStaTiming/Stage.hh` 工具（10 行 RAII Timer），把 `FastSta.cc` 压缩到 ~200 行。

---

## 收敛后的 .hh 暴露面（对外）

`flow/` / `module/` 层可见的头：

```
adapter/fast_sta/FastSta.hh                              # 单例门面
adapter/fast_sta/types/FastStaIds.hh                     # ID 类型
adapter/fast_sta/types/FastStaEnums.hh                   # FastStaNodeKind / FastStaSlewKind
adapter/fast_sta/types/FastStaContext.hh                 # FastStaClockContext (mutableClockContext 返回值)
adapter/fast_sta/types/FastStaTimingTypes.hh             # FastStaTimingPoint / FastStaDmpDriverResult
adapter/fast_sta/types/FastStaStatusTypes.hh             # FastStaCapStatus / FastStaSlewStatus / FastStaSkewSummary
adapter/fast_sta/types/FastStaPowerTypes.hh              # FastStaPowerSummary
adapter/fast_sta/types/FastStaIncrementalTypes.hh        # FastStaBufferMasterChange
adapter/fast_sta/types/FastStaCharTypes.hh               # FastStaCharTopologySpec / FastStaCharSampleResult
adapter/fast_sta/types/FastStaNodeNet.hh                 # FastStaNode (used via FastStaClockContext)
adapter/fast_sta/types/FastStaParasiticTypes.hh          # FastStaPiModel / FastStaRcNode (transitive)
adapter/fast_sta/types/FastStaLibertyTypes.hh            # FastStaLibertyCell (transitive)
adapter/fast_sta/types/FastStaGeometry.hh                # FastStaPoint (transitive)
```

flow 层不应直接 include：
- `liberty/`、`parasitics/`、`dmp_ceff/`、`clock_tree/`、`builder/`、`incremental/`、`timing/`、`power/`、`report/` 任何 .hh

### 内部"破例"问题

当前 `flow/optimization/preparation/OptimizationPreparation.cc:271` 调用 `FastStaBuilder::injectNetRouteTree` —— **必须修复**。
- **选项 A**：在 `FastSta.hh` 增加 `FastSTA::injectNetRouteTree(clock_id, net, route_tree)` 公共 API，转发到 `FastStaBuilder::injectNetRouteTree`
- **选项 B**：把 `injectNetRouteTree` 直接合并到 `FastSta.cc`，删除 `FastStaBuilder.hh` 中的对应方法

推荐 A，保持 builder 子目录的逻辑完整。

---

## 迁移步骤（建议顺序）

### Phase 1：消灭 `Internal.hh`（最重要）
1. 把 `FastStaDmpCeffInternal.hh` 改名为 `DmpSolver.hh` + `DmpLibertyLookup.hh` + `DmpNumerics.hh`，按内容拆分
2. 同时把对应 .cc 改名（`FastStaDmpCeffSolver.cc` → `DmpSolver.cc` + `DmpSolverEquations.cc`）

### Phase 2：types/ 子目录拆分
3. 把 `FastStaTypes.hh` 拆为 13 个小头文件，按主题分组
4. `FastStaTypes.cc` 改名 `FastStaLibertyTable.cc` 或保留作为 `types/` 的实现汇总

### Phase 3：每个功能子目录
5. 逐个把 `liberty/`、`parasitics/`、`clock_tree/`、`builder/`、`incremental/`、`timing/`、`power/`、`report/`、`dmp_ceff/` 落地为独立 cmake target
6. 修改顶层 `CMakeLists.txt` 改为 INTERFACE 聚合

### Phase 4：命名规范化
7. 把所有 `snapshot*` → `extract*` / `capture*`
8. 修复 `flow/optimization/preparation/OptimizationPreparation.cc:271` 的破例

### Phase 5：API 精简
9. 删除 / 移到 friend test access 的未使用 API：`changeBufferMaster`（单点）、`rebuildClockContext`、`querySinkArrival`、`queryNodeSlew`、`queryNetLoad`、`queryArea`、`queryClockIds`

### Phase 6：测试与文档
10. 在 `.trellis/spec/` 下增加 `fast-sta-architecture.md` 记录新结构
11. 更新 `test/database/adapter/fast_sta/FastSTATest.cc` 以验证子目录边界

---

## 拆分前后对比

| 维度 | 现状 | 重构后 |
|---|---|---|
| 顶层 .hh 文件数 | 13 | 1 (FastSta.hh) |
| 总文件数 | 28 | ~45（按子目录展开） |
| `Internal.hh` 数 | 1 (DmpCeffInternal) | 0 |
| CMake target 数 | 1 | 11 |
| 对外可见类 | `FastSTA` + `FastStaBuilder` + 13 个 .hh 中的所有类 | `FastSTA` + types/ POD |
| `snapshot*` 出现次数 | 18+ | 0 |
| `flow/` 直接调子模块次数 | 1 (FastStaBuilder::injectNetRouteTree) | 0 |

---

## 风险与注意事项

1. **types/ 数量爆炸**：13 个头文件可能让阅读者迷失。可考虑保留 1 个 `FastStaTypesFwd.hh` 前置声明聚合
2. **CMake target 数量 = 11**：编译时间略增，但增量编译收益巨大（修改 timing 不再 rebuild liberty）
3. **dmp_ceff 子目录的 4 个 .cc 是否过细**：可考虑合为 `DmpSolverImpl.cc` + `DmpHelpers.cc` 2 个
4. **char context vs clock context 共用 FastStaClockContext 类型**：现状已是历史选择，重构方案不改变这一点
5. **测试**：现有 `FastSTATest.cc` 调 `FastSTA::clear()`，若 API 精简删除 `clear()`，需要 friend `FastStaTestAccess` 暴露内部 reset 机制
