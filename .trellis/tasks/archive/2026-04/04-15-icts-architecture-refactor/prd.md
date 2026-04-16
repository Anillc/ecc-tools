# iCTS Architecture Refactor

## Goal

在不改变当前 iCTS 对外功能面、命令语义、主要业务结果基线的前提下，对 `src/operation/iCTS/` 做一次强约束的结构化重构。

本轮重构不以新增算法能力为目标，而是以 EDA/CTS 业务语义为中心，重建更清晰的目录、模块、target 和依赖边界，使后续算子化、算法模块化、flow 扩展和代码维护都建立在稳定骨架上。

## Confirmed Requirements

以下约束已确认，后续设计与实施以此为准：

- `source/` 下只保留三个一级模块：
  - `data_manager/`
  - `module/`
  - `utils/`
- `api/` 下只保留对外 façade：
  - `CTSAPI.hh`
  - `CTSAPI.cc`
- `flow` 不再留在 `api/` 中。
- `model/` 不再作为一级模块存在，其职责应并入 `utils/`。
- `solver/` 不再作为一级模块存在：
  - 业务级 flow / service 进入 `module/`
  - 算子、算法、内部 IR、数学/工具能力进入 `utils/`
- 如果是 EDA 业务语义层面的组件，应归入 `module/`，并在 flow 中有明确职责分工。
- 如果是算子级、算法级、基础能力级代码，应归入 `utils/`。
- 业务模块希望提供静态单例入口，但不能把运行期状态继续散落为隐式全局变量。
- 尽量细化 CMake target 与文件夹结构：
  - 只要有文件夹，就必须有 `CMakeLists.txt`
  - 只要是独立职责目录，就应有对应 target
- target 链接关系必须符合职责边界，`PUBLIC/PRIVATE/INTERFACE` 要收敛，不允许为了省事做大范围透传。
- 头文件 include 不使用相对路径；依赖路径应由 target include directories 提供。
- 能前向声明的类型必须优先前向声明。
- 非外部可见的实现细节应尽量收敛在 `.cc` 文件内部匿名 namespace 中。

## Scope

### In Scope

- 重构 `src/operation/iCTS/` 的目录结构、模块边界、target 结构和主要依赖方向。
- 调整 `api/`、`source/`、`test/` 的组织方式，使之符合新的三层目标。
- 统一 `run_cts` / `cts_report` 内部流转路径，但保持对外调用方式兼容。
- 清理当前 `model/`、`solver/`、`CTSFlow`、`CtsRuntimeRegistry` 等结构与目标架构之间的冲突点。
- 为后续 clang-format、clang-tidy TU clean、依赖环治理和脚本回归奠定结构基础。

### Out of Scope

- 本轮不新增新的 CTS 算法能力。
- 本轮不修改用户命令名、配置文件协议、报表文件名或脚本入口语义。
- 本轮不追求一次性彻底消除所有历史技术债，但新骨架必须能承接后续分阶段治理。

## Current Baseline

### Current external path

当前外部链路基本是稳定的：

`run_cts / cts_report`
-> Tcl / Python wrapper
-> `tool_manager`
-> `CtsIO`
-> `CTSAPI`

这一层对外兼容性必须保留。

### Current source layout

当前 `source/` 实际上有四个一级模块：

- `data_manager/`
- `model/`
- `module/`
- `solver/`

其中：

- `model/` 目前几乎只有 `ModelFactory`，本质是数学/拟合/求根工具，不是业务模块。
- `solver/` 混合了三类东西：
  - 单网综合流程入口
  - pipeline / operator
  - solver 私有数据结构和算法工具
- `module/` 已经开始承载业务服务，如 `router`、`committer`、`evaluator`、`runtime`
- `api/` 目前仍包含 `CTSFlow`，这和“api 只做 façade”目标冲突

### Current architectural issues

1. `model/` 语义不清晰
   - 当前只有 `ModelFactory`，更像数学工具，不应成为与 `module` 并列的一级模块。

2. `solver/` 职责过宽且名称不自然
   - “solver” 不是稳定的 EDA 业务边界。
   - 其中既有业务流程，也有算法 IR，也有工具库，语义混杂。

3. `solver` 自带一套内部数据结构，对外层形成反向侵入
   - `CtsDesign` 当前直接存放 `solver::Net*`，导致 `data_manager/database` 反向依赖 solver 内部结构。
   - 这使 `data_manager` 无法保持业务数据层纯度。

4. `api/CTSFlow` 放置位置错误
   - `CTSFlow` 是内部流程编排器，不应属于外部 API 层。

5. 全局访问和隐式上下文仍然偏重
   - `CTSAPI` 仍承担过多 façade 之外的职责。
   - `CtsRuntimeRegistry` / `GetCtsRuntime()` 仍是全局依赖入口。

6. CMake target 颗粒度和职责边界还不够稳
   - 存在大范围 `INTERFACE` 聚合 target。
   - 存在 `PUBLIC` 过宽、include directory 暴露过大、`include_directories()` 直推目录等问题。

7. include 和头文件边界仍不干净
   - 仍存在相对路径 include 和基于源树位置的 include。
   - 一些头文件之间存在可通过前向声明缓解的强耦合。

## Target Architecture

## Core Principles

- API 层只保留外部命令语义，不承载内部 flow 组织。
- `source/` 内的一级模块只保留三类：
  - `data_manager`: 数据、配置、数据库适配、导出
  - `module`: 业务语义清晰的 flow/service 组件
  - `utils`: 算子、算法、内部 IR、数学与基础能力
- 业务层调用链必须显式表达 CTS 语义：
  - load / prepare
  - synthesize
  - commit
  - evaluate
  - export / report
- 内部算法能力不直接暴露为“业务模块”。
- 目录、target、include、依赖方向必须一一对应。

## Recommended directory map

推荐将 `src/operation/iCTS/` 收敛为如下结构：

```text
iCTS/
|-- api/
|   |-- CMakeLists.txt
|   |-- CTSAPI.hh
|   +-- CTSAPI.cc
|-- source/
|   |-- CMakeLists.txt
|   |-- data_manager/
|   |   |-- CMakeLists.txt
|   |   |-- config/
|   |   |-- database/
|   |   +-- io/
|   |-- module/
|   |   |-- CMakeLists.txt
|   |   |-- flow/
|   |   |-- loader/
|   |   |-- synthesis/
|   |   |-- committer/
|   |   |-- evaluator/
|   |   +-- session/
|   |-- utils/
|   |   |-- CMakeLists.txt
|   |   |-- math/
|   |   |-- synthesis_ir/
|   |   |-- synthesis_runtime/
|   |   |-- synthesis_operator/
|   |   |-- tree_builder/
|   |   |-- timing_propagator/
|   |   +-- balance_clustering/
|   +-- ...
+-- test/
```

## Recommended role split

### `api/`

- 只保留 `CTSAPI`
- 只暴露稳定外部接口：
  - `init`
  - `runCTS`
  - `report`
  - `outputSummary`
- `CTSAPI` 内部不再直接拥有完整 flow 实现细节
- `CTSAPI` 调用 `module/flow` 提供的业务编排接口

### `source/data_manager/`

保留并纯化数据职责：

- `config/`
  - `CtsConfig`
  - `JsonParser`
- `database/`
  - `CtsDesign`
  - `CtsClock`
  - `CtsNet`
  - `CtsPin`
  - `CtsInstance`
  - `CtsSignalWire`
- `io/`
  - `CtsDBWrapper`
  - `GDSPloter`
  - report table / report output 基础设施

约束：

- `data_manager` 不应反向依赖 `utils/synthesis_ir`
- `CtsDesign` 不再直接保存 solver 私有 `Net*`
- 若需要保留中间综合结果，应由 `module/session` 或 `module/synthesis` 维护，而不是写入 `data_manager/database`

### `source/module/`

只放业务语义清晰、能直接对应 CTS flow 阶段的组件。

推荐模块：

- `flow/`
  - `CTSFlow`
  - `CTSFlowRunner`
  - 负责主流程编排
- `session/`
  - `CTSSession`
  - `CTSContext`
  - 维护一次运行的配置、设计、DB wrapper、timing/eval handle
- `loader/`
  - `DesignLoader`
  - 负责从 config / IDB / STA 读入 CTS 业务数据
- `synthesis/`
  - `ClockTreeBuilder` 或 `NetSynthesisService`
  - 负责业务层面的单网综合调度
- `committer/`
  - `DesignCommitter`
  - 负责把综合结果提交回 CTS design / IDB / timing
- `evaluator/`
  - `Evaluator`
  - `TimingAnalysisService`
  - `MetricsCollector`
  - `StatisticsWriter`
  - `DebugPlotService`

约束：

- `module` 是 flow 真正调用的业务接口层
- `module` 不直接暴露底层算法细节
- `module` 可以提供静态单例入口，但运行期数据必须通过 `CTSSession/CTSContext` 显式传入

### `source/utils/`

只放基础能力、算法、算子和内部 IR。

推荐模块：

- `math/`
  - 原 `ModelFactory`
  - 求根、拟合、数学工具
- `synthesis_ir/`
  - 原 `solver/database`
  - `Node/Pin/Inst/Net/Enum`
  - 明确标注为“综合内部 IR”，不与 CTS 业务数据库混用
- `synthesis_runtime/`
  - 原 `SolverRuntimeContext` 一类能力
- `synthesis_operator/`
  - `SinkClusteringOperator`
  - `TopologyBuilderOperator`
  - `LongWireBufferingOperator`
  - `LevelSizingOperator`
  - `NetSynthesisPipeline`
- `tree_builder/`
  - 原 `solver/tools/tree_builder`
- `timing_propagator/`
  - 原 `solver/tools/timing_propagator`
- `balance_clustering/`
  - 原 `solver/tools/balance_clustering`

约束：

- `utils` 不负责顶层业务阶段编排
- `utils` 只通过明确接口为 `module` 提供能力
- `utils` 不对外暴露“伪业务 façade”

## Module Singleton Contract

用户希望业务模块提供静态单例入口，这一要求接受，但必须加边界：

### Allowed

- 业务模块可采用 `getInst()` / `getInstance()` 形式暴露统一入口
- 适合单例暴露的对象：
  - `CTSFlow`
  - `DesignLoader`
  - `ClockTreeBuilder`
  - `DesignCommitter`
  - `Evaluator`

### Mandatory safeguards

- 单例不持有跨 run 的脏状态，或必须定义明确的 `reset()` / `init(session)` 语义
- 运行期状态必须放入 `CTSSession/CTSContext`
- 单例只作为“服务入口”，不能重新演化为新的全局 God Object
- `utils` 不使用业务单例模式

### Recommended implementation rule

- 优先使用 Meyers singleton 风格
- 单例类删除 copy/move
- 接口形态类似：
  - `CTSFlow::getInst().run(session);`
  - `DesignLoader::getInst().load(session);`
  - `ClockTreeBuilder::getInst().buildClockNet(session, clock_net);`

## Build / CMake Rules

## Directory-to-target rule

- 每个职责目录必须有自己的 `CMakeLists.txt`
- 每个职责目录必须对应一个 target
- 聚合目录可以有 `INTERFACE` target，但不能替代子目录 target

## Recommended target map

```text
icts_api

icts_data_manager
icts_config
icts_database
icts_io
icts_report

icts_module
icts_flow
icts_session
icts_loader
icts_synthesis
icts_committer
icts_evaluator

icts_utils
icts_math
icts_synthesis_ir
icts_synthesis_runtime
icts_synthesis_operator
icts_tree_builder
icts_timing_propagator
icts_balance_clustering
...
```

## Link visibility rules

- 如果头文件暴露了依赖类型，才使用 `PUBLIC`
- 仅实现需要的依赖使用 `PRIVATE`
- 聚合模块使用 `INTERFACE` 只做职责层级汇总，不做兜底依赖逃逸
- 禁止使用目录级 `include_directories()` 作为全局逃生门

## Include rules

- 禁止相对路径 include
- 禁止依赖源树位置拼接 include
- include 路径必须来自 target include dirs
- 推荐按 target 导出的语义路径引用头文件

## Header hygiene rules

- 能前向声明就不在 `.hh` 中 include 具体定义
- `.cc` 内部私有 helper、adapter、builder 优先放匿名 namespace
- 避免头文件互相包含形成依赖环

## Key Refactor Decisions

1. `CTSFlow` 从 `api/` 移出
   - 推荐移动到 `source/module/flow/`
   - 不推荐新建 `source/flow/`，因为这会破坏 `source/` 只保留三大一级模块的目标

2. `model/` 整体并入 `utils/math/`
   - `ModelFactory` 不再作为独立一级模块理由成立

3. `solver/` 不整体保留
   - `Solver` 的业务调度部分进入 `module/synthesis/`
   - `pipeline/operator/internal IR/tools` 进入 `utils/`

4. `CtsDesign` 不再保存 solver 私有 `Net*`
   - 这条是拆解 `data_manager <-> solver` 反向耦合的关键

5. `CtsRuntimeRegistry` 逐步收缩
   - 过渡期可保留兼容适配层
   - 终态应更多依赖显式 `CTSSession/CTSContext`

## Migration Plan

### Phase 1: Skeleton and target cleanup

- 建立新的 `source/module/*` 与 `source/utils/*` 目录骨架
- 为每个新目录补齐 `CMakeLists.txt`
- 建立新 target，不立刻删除旧 target
- 引入新的 include path 规则，停止新增相对路径 include

### Phase 2: Move flow out of api

- `api/` 收敛为 `CTSAPI.hh/.cc`
- 将 `CTSFlow` 迁移至 `module/flow/`
- `CTSAPI` 改为只调用 flow module 接口

### Phase 3: Dissolve model and solver

- `model/ModelFactory` -> `utils/math/`
- `solver/database` -> `utils/synthesis_ir/`
- `solver/pipeline` -> `utils/synthesis_operator/`
- `solver/tools/*` -> `utils/*`
- `solver/Solver` 的业务编排语义迁入 `module/synthesis/`

### Phase 4: Purify data_manager

- 去除 `data_manager/database` 对综合内部 IR 的直接依赖
- 让 `CtsDesign` 只维护 CTS 业务数据库对象
- 综合中间结果改由 `module/session` 或 `module/synthesis` 持有

### Phase 5: Singleton normalization and context cleanup

- 为业务模块提供静态单例入口
- 将运行期状态显式放入 `CTSSession/CTSContext`
- 缩小全局 registry 的使用范围

### Phase 6: Include / target / dependency hygiene

- 全量清理相对路径 include
- 收缩 `PUBLIC` 暴露
- 补充前向声明
- 消除明显依赖环

## Risks and Guardrails

### Main risks

- 单例模块若持有运行期状态，容易污染多次运行结果
- `data_manager` 与综合内部 IR 解耦时，可能出现中间结果丢失
- target 细化后，旧 include 路径和宽链接关系会大面积暴露问题

### Guardrails

- 单例只做服务入口，不做状态仓库
- 每阶段保留 `run_cts` / `cts_report` 回归可运行
- 优先做“结构迁移 + 接口兼容”，不同时改算法行为
- 每阶段收紧一部分 target 依赖，不做一次性全量爆破

## Acceptance Criteria

- [x] PRD 已按新的三层结构约束重写，并清理过时与冗余内容
- [x] `source/` 最终只保留 `data_manager/module/utils`
- [x] `api/` 最终只保留 `CTSAPI.hh/.cc`
- [x] `CTSFlow` 及其编排逻辑迁出 `api/`
- [x] `model/` 全量并入 `utils/`
- [x] `solver/` 被拆解为 `module + utils`，不再作为一级模块存在
- [x] `data_manager` 不再依赖综合内部 IR
- [x] 业务模块具备统一静态入口，同时运行期状态显式化
- [x] 每个职责目录都有 `CMakeLists.txt` 和 target
- [x] include 不再使用相对路径
- [x] target 链接关系收敛，`PUBLIC/PRIVATE` 符合职责边界
- [x] 新增/迁移代码优先使用前向声明与 `.cc` 内匿名 namespace
- [x] clang-format 检查通过
- [x] clang-tidy 使用 `src/utility/.clang-tidy` 检查后 TU clean
- [x] 依赖环问题被收敛或消除
- [x] 业务脚本回归通过：
  `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`

## Validation Plan

### Format

- `clang-format` 检查并修正相关文件格式

### Naming / tidy

- 使用 `src/utility/.clang-tidy` 进行命名与 TU clean 检查
- 不允许通过 `// *LIN*` 这类屏蔽方式伪 clean

### Build / runtime regression

- 完成编译
- 运行 `ics55_dev` 脚本回归
- 确认 `run_cts` / `cts_report` 行为与主要输出正常

## Research Summary

### Confirmed by current code inspection

- 当前 `model/` 仅包含 `ModelFactory`，适合作为 `utils/math`
- 当前 `solver/` 同时混合了：
  - 综合流程入口
  - operator/pipeline
  - 内部 IR
  - 算法工具
- 当前 `module/` 已经是更接近业务语义的一层
- 当前 `api/` 中的 `CTSFlow` 放置位置不符合目标架构
- 当前 `data_manager/database/CtsDesign` 直接依赖 solver 私有 `Net*`，这是关键耦合点

### Recommended architecture choice

推荐采用：

- `api` 仅 façade
- `module` 承担业务 flow / service
- `utils` 承担算法 / 算子 / 内部 IR / 数学能力
- `data_manager` 承担配置 / CTS 数据库 / IDB 适配 / IO

这是当前代码演进方向与用户目标约束之间最一致、风险最低的一条路径。
