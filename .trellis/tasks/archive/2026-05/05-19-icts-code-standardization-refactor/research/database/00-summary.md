# Research: database 层最严重的 5 个问题 + fast_sta 拆分方案概要

- **Query**: 收尾总结（≤400 字）
- **Date**: 2026-05-19

---

## fast_sta 拆分方案概要

按"高内聚低耦合 + 每个子模块独立 CMake target"原则，把 28 个文件拆为 11 个子目录（顶层只剩 `FastSta.hh/.cc`）：
1. `types/`（13 个 POD 头）作 INTERFACE 库
2. `dmp_ceff/`（5 个 .cc + 3 个私有 .hh）替代 `FastStaDmpCeffInternal.hh`，文件改名 `DmpSolver/DmpLibertyLookup/DmpNumerics`
3. 其余按职责分：`liberty/` `parasitics/` `clock_tree/` `builder/` `incremental/` `timing/` `power/` `report/`

外部仅可见 `FastSta.hh` + `types/` 头；`FastStaBuilder` 不再被 flow 层直接调用（修复 `OptimizationPreparation.cc:271` 这个破例）；删除 7 个未用 API；所有 `snapshot*` 改为 `extract*`/`capture*`。

---

## database 层最严重的 5 个问题

1. **fast_sta/ 全平铺 28 文件无子目录，且 `FastStaDmpCeffInternal.hh` 暴露 169 行私有求解器类** —— 缺少子目录与 CMake 隔离，所有 13 个 .hh 通过 PUBLIC include 暴露给外部；`FastStaBuilder::injectNetRouteTree` 被 flow 层直接调用，门面 FastSta.hh 失效

2. **互联网化命名泛滥**：18+ 处 `snapshot*`（FastStaLiberty/FastStaBuilder/WrapperClockWriterSupport），4 处 `Internal` 文件（FastStaDmpCeffInternal、STAAdapterInternal、ClockTraceResolverInternal、WrapperClockWriterInternal），1 处 `Wrapper`（整个 io/ 目录），1 处 `Support`（WrapperClockWriterSupport.cc）

3. **design/ 内 ClockLayout 与 ClockNetwork 大量重复枚举与 struct**：DomainKind/InstRole/NetRole 各有两套等价定义（`ClockLayout.hh:36-77` vs `ClockNetwork.hh:39-65`），4 个 Clock* 类边界不清

4. **STAAdapterInternal / ClockTraceResolverInternal 各塞 25 + 50 个自由函数**：sta/sdc 两个 adapter 子目录都用单一大 Internal 头共享辅助函数，本应按职能拆为 `internals/units/`、`internals/liberty/`、`clock_trace/` 等子目录

5. **CONFIG_INST/DESIGN_INST/WRAPPER_INST 单例宏命名宽泛**：缺 `CTS_` 前缀；`Config` 单例混合算法参数与文件路径 30+ 字段；`Wrapper` 是双重违例（命名 + 全局单例）

---

## 写出的研究文件

- `research/database/01-fast_sta-deep-dive.md` - 完整文件清单、API 暴露面、内部依赖、命名问题、.hh/.cc 分布
- `research/database/02-naming-issues.md` - 全 database 层命名违例扫描，按类别分组
- `research/database/03-submodule-overview.md` - 8 个子目录职责梳理与数据流
- `research/database/04-fast_sta-refactor-proposal.md` - 拆分方案的具体目录结构、CMake target 划分、迁移步骤
