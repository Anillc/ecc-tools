# Research: database 层全局命名问题扫描

- **Query**: 扫描 `source/database/` 整个目录，找出所有互联网化命名 (Internal/Support/Snapshot/Request/Response/Manager/Handler/Helper/Wrapper/Util)
- **Scope**: internal (`src/operation/iCTS/source/database/`)
- **Date**: 2026-05-19

---

## 概览：发现的 4 类问题样本

### 类别 1：`Internal` 文件 / 命名空间（3 处）

| 文件路径 | 行 | 当前内容 | 用途 | CTS 语义化建议 |
|---|---|---|---|---|
| `adapter/fast_sta/FastStaDmpCeffInternal.hh` | 169 | `namespace fast_sta_dmp` + `DmpSolver` 类全声明 + 8 个自由函数 + 17 个常量 | 让 `DmpCeff*.cc` 共享 `DmpSolver` 私有类型 | 把 DmpSolver 整合到单一 .cc 内（消除 Internal.hh）；或拆为 `dmp_ceff/` 子目录，文件改名 `DmpSolver.hh` + 子 cmake target，仅子目录内可见 |
| `adapter/sta/STAAdapterInternal.hh` + `.cc` | 96 + 大量行 | `namespace sta_adapter_internal` + `WireRcProbe` struct + 25 个自由函数 + 2 个常量 | 让 7 个 `STAAdapter*.cc` 共享 ista TimingEngine/IDB 桥接函数 | 拆为 `sta/internals/` 子目录，按功能分文件：`StaEngineAccess.hh`、`LibertyLookup.hh`、`UnitConversion.hh`、`InstResolution.hh` |
| `adapter/sdc/ClockTraceResolverInternal.hh` | 155 | `namespace icts::clock_trace` + 50+ 个自由函数 + 9 个 struct | 让 4 个 `ClockTrace*.cc` 共享 SDC 走线辅助函数 | 拆为 `sdc/clock_trace/` 子目录，按职责分：`SdcRefResolve.hh`、`SinkClassifier.hh`、`PinClassification.hh` + `ReportEmit.hh` |
| `io/WrapperClockWriterInternal.hh` | 67 | 3 个 struct（含 `IdbNetPinSnapshot`、`ClockIdbWriteScope`、`ClockIdbWriteBackup`） + 4 个自由函数 | 让 `WrapperClockWriter.cc` 与 `WrapperClockWriterSupport.cc` 共享回写状态 | 把这些 struct 移到 `ClockIdbWriter.hh`（按对象拆分而不是按 "Internal"） |

**评估**：所有 `Internal` 文件都是 .cc 切分的副产物。当一个逻辑模块被拆成多个 .cc 时，C++ 强迫共享类型必须放在头文件中。"Internal" 命名约定 vs CMake target 隔离 vs 子目录隔离，目前 iCTS 选了最弱的方式（命名约定）。

---

### 类别 2：`Snapshot` 词汇（互联网/数据库化用语）

| 文件 | 行 | 当前命名 | 上下文 | CTS 语义化建议 |
|---|---|---|---|---|
| `io/WrapperClockWriterInternal.hh` | 42 | `struct IdbNetPinSnapshot` | iDB 网络 pin 列表的备份/快照 | `IdbNetPinBackup` 或 `IdbNetPinState` |
| `io/WrapperClockWriterInternal.hh` | 65 | `auto SnapshotIdbNetPins(...)` | 函数名 | `captureIdbNetPins` / `recordIdbNetPins` |
| `io/WrapperClockWriterInternal.hh` | 58 | `net_pin_membership_by_name` map of snapshots | 字段名 | 同上 |
| `adapter/fast_sta/FastStaLiberty.hh` | 21 | doc: "Liberty data snapshots extracted" | 注释 | "Liberty data captured for CTS cells" |
| `adapter/fast_sta/FastStaLiberty.hh` | 37 | `snapshotBufferCell` | 静态方法 | `captureBufferCellLiberty` / `extractBufferLiberty` |
| `adapter/fast_sta/FastStaLiberty.cc` | 21 | doc: "Liberty data snapshot implementation" | 注释 | 同上 |
| `adapter/fast_sta/FastStaLiberty.cc` | 90-133 | `snapshotTable`、`snapshot.kind`、`snapshot.values` | 局部函数与变量 | `extractTable`、`out.kind` 等 |
| `adapter/fast_sta/FastStaLiberty.cc` | 136 | `snapshotDelayTable` | 函数 | `extractDelayTable` |
| `adapter/fast_sta/FastStaLiberty.cc` | 146 | `snapshotPowerTable` | 函数 | `extractPowerTable` |
| `adapter/fast_sta/FastStaLiberty.cc` | 202 | `snapshotPowerArcTables` | 函数 | `extractPowerArcTables` |
| `adapter/fast_sta/FastStaLiberty.cc` | 223 | `snapshotBufferCellFromLibCell` | 函数 | `extractBufferCellFromLibCell` |
| `adapter/fast_sta/FastStaBuilder.cc` | 93 | comment "FastSTA snapshots CTS-owned topology here" | 注释 | "FastSTA captures CTS-owned topology" |
| `adapter/fast_sta/FastStaBuilder.cc` | 119 | `snapshotClockData` | 函数 | `materializeClockData` |
| `adapter/fast_sta/FastStaBuilder.cc` | 157 | `snapshotSinkPinCaps` | 函数 | `assignSinkPinCaps` |
| `adapter/fast_sta/FastStaBuilder.cc` | 188 | log "snapshot sink pin caps" | 日志字符串 | "captured sink pin caps" |
| `adapter/fast_sta/FastStaBuilder.cc` | 192 | log "snapshot liberty and clock data" | 日志字符串 | "captured liberty and clock data" |
| `io/WrapperClockWriterSupport.cc` | 96-108 | `SnapshotIdbNetPins` / `IdbNetPinSnapshot snapshot` | 函数实现 + 变量 | `captureIdbNetPins` + `IdbNetPinBackup backup` |

**模式**：`snapshot*` 在 fast_sta 与 io 两处大量使用，是 Web/DB 思维。CTS 语义对应的概念是 **capture / extract / materialize**（从 iSTA/iDB 中"拷贝/物化"成 CTS 可独立操作的数据）。

---

### 类别 3：`Support` 词汇

| 文件路径 | 当前命名 | 用途 | CTS 语义化建议 |
|---|---|---|---|
| `io/WrapperClockWriterSupport.cc` | 文件名 "Support" | 实现 `WrapperClockWriterInternal.hh` 中的 4 个自由函数（操作 IdbNet pins） | `WrapperClockWriterIdbAccess.cc` 或并入 `WrapperClockWriter.cc` |
| `io/WrapperClockWriterSupport.cc:21` | doc "Internal clock writeback support routines" | 注释 | "iDB net pin mutation helpers for clock writeback" |
| `characterization/HTreeTopologyChar.hh:37` | comment "Supports composition for H-tree concatenation via Hash-Join" | 描述 | OK（自然语言，非命名） |
| `characterization/SegmentChar.hh:35` | comment "Supports composition for segment concatenation via Hash-Join" | 描述 | OK |
| `characterization/BufferingPattern.hh:84` | comment "Supports concatenation for segment composition" | 描述 | OK |

---

### 类别 4：`Wrapper` 类（核心 IO 模块）

| 文件路径 | 行 | 当前命名 | 评估 |
|---|---|---|---|
| `io/Wrapper.hh` | 74 | `class Wrapper` 单例 + `WRAPPER_INST` 宏 + `WrapperCellGeometry` + `WrapperWriteResult` + `Wrapper::CtsClockReader` + `Wrapper::CtsClockIdbWriter` | "Wrapper" 是经典互联网化命名（包裹什么？iDB） |
| `io/Wrapper.hh:18` | doc "DB wrapper for iCTS" | 自己也承认是"DB wrapper" | 应改为 `IdbBridge` / `IdbExchange` / `CtsIdbAccess` |
| `io/Wrapper.hh:46` | `WRAPPER_INST` 宏 | 单例访问 | `IDB_BRIDGE_INST` / `CTS_IDB_INST` |
| `io/Wrapper.hh:56` | `WrapperCellGeometry` | iDB cell 的几何视图 | `IdbCellGeometryView` |
| `io/Wrapper.hh:65` | `WrapperWriteResult` | clock 回写结果 | `ClockWriteResult` |
| `io/Wrapper.hh:126-129` | 内部友元 `CtsClockReader` / `CtsClockIdbWriter` | 实现细节 | OK（已用 friend 隔离） |
| `io/WrapperClockReader.cc` | 文件 | clock 读取实现 | `IdbClockReader.cc` |
| `io/WrapperClockWriter.cc` | 文件 | clock 回写实现 | `IdbClockWriter.cc` |
| `io/WrapperClockWriterSupport.cc` | 文件 | "Support" 双重违例 | `IdbClockWriterAccess.cc` |
| `io/WrapperClockWriterInternal.hh` | 文件 | "Internal" 双重违例 | `IdbClockWriterTypes.hh` |

**结论**：`io/` 整个目录用 "Wrapper" 命名，需要重命名为 `IdbBridge` 或 `IdbAccess`，并把所有 `Wrapper*` 前缀文件改为 `Idb*` 或 `IdbClock*`。

---

## 没有发现的"互联网化"词

| 词 | database 中是否出现？ | 说明 |
|---|---|---|
| `Request` | 否 | clean |
| `Response` | 否 | clean |
| `Manager` | 否 | clean |
| `Handler` | 否 | clean（注：sdc 子目录的 .cc 注释里出现 "Internal SDC clock command handlers"，文件名是 `SdcClockCommands.cc`，OK） |
| `Helper` | 否 | clean |
| `Util` | 否 | clean |

---

## 类别 5：单例 + Inst 宏 命名风格

| 文件 | 单例宏 | 评估 |
|---|---|---|
| `adapter/fast_sta/FastSta.hh:38` | `FAST_STA_INST` | OK，前缀清晰 |
| `adapter/sta/STAAdapter.hh:44` | `STA_ADAPTER_INST` | OK |
| `config/Config.hh:35` | `CONFIG_INST` | 太宽泛，应该 `CTS_CONFIG_INST` |
| `design/Design.hh:40` | `DESIGN_INST` | 太宽泛，应该 `CTS_DESIGN_INST` |
| `io/Wrapper.hh:47` | `WRAPPER_INST` | 双重问题：宽泛 + Wrapper 命名 |

---

## 类别 6：Reader / Writer / Resolver / Builder（类名后缀）

虽然这些是经典 Java/C# 后缀，C++ 圈也用，但需要审视是否真表达了"领域行为"：

| 类 | 文件 | 评估 |
|---|---|---|
| `class SdcClockReader` | `adapter/sdc/SdcClockReader.hh:34` | OK——确实是从 SDC 文件读 |
| `class ClockTraceResolver` | `adapter/sdc/ClockTraceResolver.hh:61` | OK——解析 SDC clock 到 net |
| `class FastStaBuilder` | `adapter/fast_sta/FastStaBuilder.hh:38` | OK——构造 context |
| `class Wrapper::CtsClockReader` | `io/WrapperClockReader.cc:175` | 内嵌类，OK |
| `class Wrapper::CtsClockIdbWriter` | `io/WrapperClockWriter.cc:52` | 内嵌类，OK |
| `class SdcSubsetEvaluator` | `adapter/sdc/SdcClockParser.hh:73` | "Evaluator" + "Subset"，OK |
| `class ArithmeticParser` | `adapter/sdc/SdcClockParser.hh:52` | OK |

这些都是真实的 "动作 + er" 抽象，没有问题。

---

## 优先级排序

### 高优先级（影响外部 API 与多文件）

1. `io/Wrapper.*` → 重命名为 `io/IdbBridge.*` 或 `io/CtsIdbAccess.*`（影响整个 io/ 目录）
2. `adapter/sta/STAAdapterInternal.{hh,cc}` → 拆为 `sta/internals/` 子目录 + 多文件
3. `adapter/sdc/ClockTraceResolverInternal.hh` → 拆为 `sdc/clock_trace/` 子目录

### 中优先级（影响 fast_sta 内部）

4. `adapter/fast_sta/FastStaDmpCeffInternal.hh` → 合并到一个 .cc 或拆为 `dmp_ceff/` 子目录
5. `adapter/fast_sta/FastStaLiberty.*` 中的所有 `snapshot*` → `extract*` / `capture*`
6. `adapter/fast_sta/FastStaBuilder.cc` 中的 `snapshot*` → `capture*` / `materialize*`

### 低优先级（注释与 doc 文字）

7. `*.hh` 顶部注释中的 "snapshot" 替换为 "extract" / "capture"
8. `CONFIG_INST` / `DESIGN_INST` 加 `CTS_` 前缀
