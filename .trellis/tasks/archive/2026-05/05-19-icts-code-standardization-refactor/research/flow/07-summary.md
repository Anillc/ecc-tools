# Research: flow 层调研总结

- **Query**: 提炼 flow 层最严重的 5 个问题（不超过 300 字）
- **Scope**: internal
- **Date**: 2026-05-19

## 调研产出文件

| 文件 | 主题 |
|---|---|
| `01-overview.md` | 顶层 Flow 类与编排逻辑 |
| `02-subflow-patterns.md` | 六个 sub-flow 的入口、阶段、状态 |
| `03-paradigm-consistency.md` | 范式一致性与 init/run/report 适配度 |
| `04-naming-issues.md` | Internal/Request 等命名扫描 |
| `05-file-organization-and-cmake.md` | 文件大小、.hh 拆分、CMake 粒度 |
| `06-additional-issues.md` | 耦合、重复定义、错误传播 |
| `07-summary.md` | 本文件 |

## 最严重的 5 个问题（约 290 字）

1. **范式不统一**：六个 sub-flow 入口动词混用（`initialize`/`run`/`evaluate`/`build`/`emit`），返回类型五花八门（`bool`/`SynthesisTraceSummary`/`OptimizationResult`/`EvaluationResult`/`ReportResult`/`InstantiationResult`/`IdbConversionResult`），错误信号混合 `bool success`、`SynthesisOutcome` 枚举、`evaluation_ready` 多套，没有任何一个 sub-flow 把 init/run/report 显式拆为三阶段，全部塞进单个 `run`。
2. **命名空间与目录不对齐**：同一物理目录下子文件使用 `icts`、`icts::htree`、`icts::htree::analytical_solver`、`icts::optimization_internal`、`icts::qor_evaluation`、`icts::visualization` 等 6+ 种 namespace 混排；`_internal` 后缀直接外泄到对外可见的 namespace 与 `*Internal.hh` 文件（4 处），违反 PRD 命名约定。
3. **跨 sub-flow 边界破裂**：Report 直接调用 Evaluation::run（`Report.cc:62`）；Synthesis 子模块 PRIVATE link Instantiation 子模块（`synthesis/distribution/CMakeLists.txt:12`）；`DesignConversion` 放在 `instantiation/` 但被 Flow/Topology/Instantiation 三处共用。
4. **Flow 单例职责过重**：`Flow.hh:38-78` 既编排又持有 5 类状态（`CharacterizationLibrary` 等 sub-sub-flow 内部类型），公共 API 11 个、CTSAPI 注入 setup_ready 形成跨层契约。
5. **文件粒度失衡**：`SegmentLibrary.hh` 563 行混入 13 个类；`OptimizationTypes.hh` 230 行 21 个结构；htree/optimization 子目录各 8-12 个 1-cc target，CMake 维护爆炸（50 个 CMakeLists、43 个 sub-target），同时 `HTree::BuildOptions`/`Topology::BuildOptions`/`SourceTrunkSegment::BuildOptions` 名字撞车增加歧义。

## 文件清单（绝对路径）

- /home/liweiguo/project/ecc-tools/.trellis/tasks/05-19-icts-code-standardization-refactor/research/flow/01-overview.md
- /home/liweiguo/project/ecc-tools/.trellis/tasks/05-19-icts-code-standardization-refactor/research/flow/02-subflow-patterns.md
- /home/liweiguo/project/ecc-tools/.trellis/tasks/05-19-icts-code-standardization-refactor/research/flow/03-paradigm-consistency.md
- /home/liweiguo/project/ecc-tools/.trellis/tasks/05-19-icts-code-standardization-refactor/research/flow/04-naming-issues.md
- /home/liweiguo/project/ecc-tools/.trellis/tasks/05-19-icts-code-standardization-refactor/research/flow/05-file-organization-and-cmake.md
- /home/liweiguo/project/ecc-tools/.trellis/tasks/05-19-icts-code-standardization-refactor/research/flow/06-additional-issues.md
