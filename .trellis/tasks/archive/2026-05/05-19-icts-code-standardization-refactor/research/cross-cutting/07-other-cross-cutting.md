# Research: Other Cross-Cutting Issues

- **Query**: 文档/注释/代码风格、测试覆盖、external_libs、异常/日志/单例/类型重复/智能指针等额外问题
- **Scope**: internal
- **Date**: 2026-05-19

## 7.1 external_libs/ 分析

```
external_libs/
├── CMakeLists.txt
├── icts_api_external_libs.cmake     → idm, ista-engine, log, usage, feature_db
├── icts_source_external_libs.cmake  → geometry_db, idb, IdbBuilder, def_builder, lef_builder, def_service, lef_service
└── icts_test_external_libs.cmake    → gtest, pthread + 同 source 的 idb 链
```

### 是第三方库吗？
**全部不是第三方**——所有名字都是 iEDA 内部 target：
- `idm` → `src/platform/data_manager/idm`
- `ista-engine` → `src/operation/iSTA`
- `log` / `usage` → `src/utility/log`, `src/utility/usage`
- `feature_db` → `src/feature/feature_db`
- `geometry_db` → `src/database/manager/geometry_db` (推测)
- `idb` / `IdbBuilder` / `def_builder` / `lef_builder` / `def_service` / `lef_service` → `src/database/`
- `gtest`, `pthread` → 真正的第三方

### 问题
- **`external_libs/` 是个误导名字**：里面 90% 是 iEDA 项目内部 target（idm / idb / ista-engine / log / usage 等），只有 gtest 和 pthread 算外部。
- 实际职责是"声明 iCTS 对 iEDA 其他模块的引用"——更精确的名字是 `dependencies/` 或 `linkage/`。
- 内容很薄（3 个 .cmake 文件 + 1 个 CMakeLists.txt 共 ~30 行），所以并不需要拆分；但命名误导值得记录。

### 是否应该合并/提取？
- 三个 .cmake 文件可以合并为一个，按需要的库列出（api/source/test），避免维护 3 个并行结构。

## 7.2 注释、文档、代码风格

### 7.2.1 文件头版权
- 抽查显示**所有 .hh/.cc 都有统一 Mulan PSL v2 copyright 块**（多次抽样确认）。
- 文件头紧跟 doxygen `@file/@author/@date/@brief`，作者基本是 `Dawn Li (dawnli619215645@gmail.com)`。
- `grep -rL "Copyright" .../source --include="*.hh,*.cc"` 返回空 → **没有缺失版权的文件**。
- 326 个源文件全部含 `@file`。

### 7.2.2 TODO / FIXME / HACK 状况
- `grep -rEi "todo|fixme|xxx|hack"` 跨整个 source 返回 **0 行**。
- 说明：要么 TODO 都已清干净（最近的 cts 重构任务 archive 5 个），要么开发者用其他渠道追踪（issue / 任务系统）。
- 没有"TODO/FIXME 堆积"问题。

### 7.2.3 注释风格
- 头文件 `@brief` 普遍 1 句话，没有过度。
- `Geometry.hh` 抽样：`Manhattan` / `CalcCenter` / `CalcMedian` / `ProjectToL1Circle` 都有 `@brief` doxygen 注释，对返回类型 / 参数类型限制说清楚。**质量良好**。
- `Schema.hh`：声明部分干净，无内部实现注释（注释都在 .cc 里）。

### 7.2.4 .clang-format
- 仓库根有 `.clang-format`（C++20, Google base, ColumnLimit=140, BraceWrapping Custom）。
- iCTS 没有自己的 `.clang-format`，跟随仓库根。
- 抽样检查（Geometry.hh / Schema.hh / SegmentLibrary.hh）：函数 `auto Name() -> T` 风格统一，类后大括号换行（AfterClass=true）一致。**符合**根 clang-format。

### 7.2.5 命名空间风格
统计实际出现的 namespace（去重）：
```
icts                              (主命名空间)
icts::analytical
icts::bst
icts::clock_trace                 (sdc 子模块)
icts::fast_clustering             (module/topology/fast_clustering)
icts::fast_sta_dmp                (database/adapter/fast_sta dmp 部分)
icts::geometry                    (utils/geometry)
icts::graph                       (utils/graph)
icts::htree                       (flow/synthesis/htree)
icts::htree::analytical_selection
icts::htree::analytical_solution
icts::htree::analytical_solver
icts::logformat                   (utils/logger)
icts::optimization_internal       ←── 13 个文件
icts::qor_evaluation              (flow/evaluation/qor)
icts::schema                      (utils/logger)
icts::sdc_reader                  (database/adapter/sdc)
icts::sta_adapter_internal        (database/adapter/sta)
icts::topology
icts::visualization
icts::visualization::cluster
icts::visualization::detail
icts::visualization::topology
icts_test                         (test/)
```

### 风格不齐
- 大部分子模块用 `icts::<area>`（geometry / graph / htree / topology …）。
- 但有 `icts::optimization_internal` 和 `icts::sta_adapter_internal` 用 `_internal` 后缀，与其他子模块**风格不一致**。
- `icts::htree::analytical_selection / analytical_solution / analytical_solver` 是嵌套两层，目录层级是 `flow/synthesis/htree/analytical_solver/` —— flow/synthesis 这一层被吞掉了。
- 期望统一规则（spec 候选）：
  - 子模块命名空间：`icts::<feature>` 或 `icts::<feature>::<sub>`
  - 不使用 `_internal` 后缀，改用 `icts::<feature>::detail` 或匿名 namespace

## 7.3 测试覆盖

### 7.3.1 test/ 与 source/ 的对应关系

source/ 目录 → test/ 目录对应表：

| source/ | test/ | 状态 |
|---|---|---|
| utils/geometry | test/utils/geometry/ | 空目录（无 CMake、无 cc） |
| utils/graph | test/utils/graph/RootedTreeLCATest.cc | ✓ 有 |
| utils/logger | test/utils/logger/ | 空目录（无 CMake、无 cc） |
| utils/visualization | test/common/visualization/ | 不直接对应（在 common 中） |
| database/config | test/database/config/ | 空目录 |
| database/design | test/database/design/* | ✓ |
| database/io | test/database/io/ | 空目录 |
| database/spatial | test/database/spatial/SpatialRegionTest.cc | ✓ |
| database/adapter/fast_sta | test/database/adapter/fast_sta/ | ✓ |
| database/adapter/sta | （无对应） | 缺 |
| database/adapter/sdc | （无对应） | 缺 |
| database/routing | （无对应） | 缺 |
| database/timing | （无对应） | 缺 |
| database/characterization | （无对应） | 缺 |
| database/qor | （无对应） | 缺 |
| module/topology | test/module/topology/* | ✓ |
| module/characterization | test/module/characterization/* | ✓ |
| module/routing | test/module/routing/* | ✓ |
| module/timing | test/module/timing/.gitkeep | 占位 |
| module/analytical_characterization | test/module/analytical_characterization/* | ✓ |
| flow/* | test/flow/* | ✓ |

### 7.3.2 占位（仅 .gitkeep）目录
```
test/module/buffering/.gitkeep
test/module/buffer_sizing/.gitkeep
test/module/drv/.gitkeep
test/module/optimization/.gitkeep
test/module/report/.gitkeep
test/module/timing/.gitkeep
```
这些 test 目录"已经预留位置"但 source 里**没有对应的 module 子目录**：buffering、buffer_sizing、drv、report 在 source/module/ 里不存在。

**结论**：测试目录"提前规划"了未来子模块，但 source 还未对应。这是 **planning artifact**，不算 bug 但有方向不一致。

### 7.3.3 测试覆盖缺口
- 数据库：spatial 有，design 有；config / io / routing / timing / characterization / qor 都缺。**6 个核心数据库子模块没单测**。
- utils：graph 有；geometry / logger / visualization 都缺。**3/4 没单测**。
- adapter：fast_sta 有；sta、sdc 都缺（sdc 最近增长大）。

### 7.3.4 test 内部 Support 文件爆炸
- `test/**/*Support*` 共 **31 个文件**（在 04-naming-scan.md 详述）。
- test/ 自有 helper 库较多：`test/common/clustering/{artifact,metrics}/`、`test/common/realtech/{asset,load,support}/`、`test/common/topology/`、`test/common/visualization/`、`test/common/data/{distribution,pin_factory}/`、`test/common/io/`、`test/common/logging/` — 测试基础设施分布很广。
- **test 本身已经成一个 mini 项目**，需要单独的代码风格规范。

## 7.4 异常处理

- `grep -rEn "throw |try \{|catch \(" source/` 只有 **2 处**，都在 `database/config/Config.cc` 里：
  - Line 76–78：尝试-捕获（用 `catch (...)`）
  - Line 108–114：尝试-捕获（用 `catch (...)`）
  - Line 260–262：`catch (const nlohmann::json::exception& error)`
- 这意味着 iCTS 整体策略是 **"不抛异常"**，错误用 `LOG_FATAL_IF` 终止程序或返回 `bool/optional`。
- 例外是 Config.cc，因为它要读 JSON，nlohmann::json 会抛异常 → 必须 catch。

### `LOG_FATAL_IF` 滥用？
- `LOG_FATAL_IF` 出现 264 次，是主要的"错误处理"机制。
- 抽样 `SegmentLibrary.hh`：
  ```cpp
  LOG_FATAL_IF(it == composition_states.end()) << "HTree: missing segment pattern composition-state cache entry.";
  ```
  这类调用很多。意味着错误 = 程序终止，没有可恢复路径。
- 对一个"分析/综合工具"来说，FATAL 一切错误并非不合理（错误数据无法恢复），但 264 次说明**契约检查（不变量）和真正的运行时错误没区分**。两者都用 LOG_FATAL_IF 会让"逻辑 bug"和"输入错误"难以区分。
- spec 候选：分离 `LOG_FATAL_IF`（程序内部不变量）与 `LOG_ERROR_RETURN`（外部输入错误，应可恢复或汇报）。

## 7.5 全局状态 / 单例

工程内共 **7 个单例**（含 1 个 schema）：
1. `Flow::getInst()` → `FLOW_INST` （flow/Flow.hh）
2. `Config::getInst()` → `CONFIG_INST` （database/config/Config.hh）
3. `Wrapper::getInst()` → `WRAPPER_INST` （database/io/Wrapper.hh）
4. `STAAdapter::getInst()` → `STA_ADAPTER_INST` （database/adapter/sta/STAAdapter.hh）
5. `FastSTA::getInst()` → `FAST_STA_INST` （database/adapter/fast_sta/FastSta.hh）
6. `Design::getInst()` → `DESIGN_INST` （database/design/Design.hh）
7. `SchemaWriter::getInst()` → `SCHEMA_WRITER_INST` （utils/logger/Schema.hh）

`api/CTSAPI` 也是单例（第 8 个）。

### 问题
- **没有任何机制保证 `CTSAPI::resetAPI()` 调用了所有单例的 `reset()`**：当前手动列举 5 个（CONFIG/DESIGN/WRAPPER/FLOW/SCHEMA_WRITER），但 STA_ADAPTER 和 FAST_STA 不在列表里。
  - 实际：`STA_ADAPTER_INST` 内部如果有持久化状态（lib cache、vertex cache 等），多次 `init` 会累积。**潜在 bug**。
- 单例形成"看不见的依赖图"：任何 .cc include `Config.hh` 就能用 CONFIG_INST，没有 ScopedConfig / ConfigContext / dependency injection。
- 多 clock domain / 并行流程几乎不可能（单例假设一次只有一个 CTS run）。

## 7.6 类型定义重复 / enum 不规范

### `SinkDomainKind` 跨多个 hh 引用
6 处引用，但定义只在 `database/design/ClockLayout.hh:64`：
- `database/design/ClockLayout.hh`（owner）
- `flow/instantiation/design_conversion/DesignConversion.hh:37`（forward declare）
- `flow/synthesis/trace/domain_status/DomainStatus.hh:34`（forward declare）
- `flow/synthesis/distribution/ClockDistribution.hh`（using）
- `flow/report/visualization/drawing/Drawing.hh`（using）
- `flow/report/visualization/gds/layer/LayerPolicy.hh`（using）

OK：定义唯一，多处使用 forward declaration。

### "Kind" 后缀的 enum class 太多
完整列表：
```
LoadCountKind                (module/topology/TopologyGen.hh)
ClusterRouterKind            (module/topology/config/TopologyConfig.hh)
LayerKind                    (flow/report/visualization/gds/layer/LayerPolicy.hh)
SinkDomainKind               (database/design/ClockLayout.hh)
SegmentFrontierKind          (flow/synthesis/htree/segment_pruning/SegmentLibrary.hh)
TopologyPatternNodeKind      (flow/synthesis/htree/segment_pruning/SegmentLibrary.hh)
DomainKind                   (database/design/ClockNetwork.hh)
TopologyKind                 (database/design/ClockNetwork.hh)
SdcObjectKind                (database/adapter/sdc/SdcClockModel.hh)
SdcObjectKind::Kind          (内嵌)
FastStaNodeKind              (database/adapter/fast_sta/FastStaTypes.hh)
FastStaLibertyTableKind      (database/adapter/fast_sta/FastStaTypes.hh)
FastStaLibertyAxisKind       (database/adapter/fast_sta/FastStaTypes.hh)
```

观察：
- `DomainKind` 和 `SinkDomainKind` 看起来语义相近，但是不同定义 — 需要确认是否真的不同概念。
- `TopologyKind`（ClockNetwork）和 `TopologyPatternNodeKind`（SegmentLibrary）也是分散的，可能需要统一在一处。

### Enum 命名风格
- 所有 enum class 一律用 `kXxx` 命名（kSource / kRise / kFall / kUnknown），符合 Google C++ style → 风格一致。

## 7.7 内存管理 / 智能指针

- `grep` 显示 .hh 中智能指针/裸 new/delete 共 **35 次**，只有 `std::unique_ptr` 和 `std::shared_ptr` 的使用，**没有任何**裸 `new T`、`delete `（除 `= delete` 的 special-member-deletion 之外）。
- 抽样：`utils/logger/Schema.hh:99` 有 `std::unique_ptr<ieda::Stats> _stats;`。
- 主流方式用值/引用语义；少量需要 polymorphism 的用 unique_ptr。
- **没有内存管理风险**（基于抽样）。

## 7.8 编译选项 / debug 宏

- 整个 source/ 没有 `#ifdef DEBUG` 或 `#ifndef NDEBUG`。
- 顶层 CMakeLists.txt 有 10+ 个 `option(DEBUG_ICTS_XXX OFF)`，**只影响编译选项**（`-g3 -O0`），不进入代码 — 干净的做法。
- 没有"DEBUG 宏滥用"。

## 7.9 综合 cross-cutting 发现（重复用户没明确点名的）

1. **`external_libs/` 命名误导** — 内容 90% 是项目内部模块的链接配置。
2. **测试覆盖严重不均** — 6 个数据库子模块、3 个 utils 子模块缺单测；部分 `*.gitkeep` 表示规划但未交付。
3. **264 次 LOG_FATAL_IF** — 错误恢复策略缺失，所有错误都终止程序。
4. **7 个单例 + 隐式 reset 契约** — 全局状态散布；resetAPI 不全。
5. **`*_internal` namespace 风格不齐** — 部分子模块用 `_internal` 后缀，其他用领域词。
6. **Enum class 散布但命名风格一致** — 不需要重命名，但分布需要梳理（DomainKind / SinkDomainKind / TopologyKind / TopologyPatternNodeKind 等是否重复）。
7. **clang-format 一致**、**版权头一致**、**doxygen `@file` 一致** — 这些维度没有问题，是项目的优点。
8. **0 TODO/FIXME** — 工程纪律好。

## Caveats / Not Found

- 测试覆盖只看了目录对应关系，没看每个测试的具体覆盖率（line coverage）。
- enum 重复仅基于命名抽样，没逐个比对枚举值。
- 单例 reset 是否真的"会漏" 需要跟踪每个单例的状态字段，本文只从 `resetAPI` 列表反推。
