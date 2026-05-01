# Research: subflow report architecture

- Query: Research the proposed `report` subflow for CTS flow architecture and naming redesign. Inspect current report step, visualization, clock-tree view, evaluation statistics, schema/log report output, SVG/GDS artifacts, and Tcl/API report calls. Determine report responsibilities so the architecture does not look like report only contains visualization. Propose readable second-level report subfolders.
- Scope: internal
- Date: 2026-04-30

## Findings

### Files found

| Path | Description |
|---|---|
| `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.hh` | Final report step interface returning `report_success` and `evaluation_ready`. |
| `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc` | Current report orchestration: path resolution, report mode logging, optional evaluation rebuild, statistics reports, SVG/GDS generation, runtime/status summary. |
| `src/operation/iCTS/source/flow/FlowManager.hh` | Top-level flow state holder for run summary, clock-tree view, evaluation state, writeback state, and report API. |
| `src/operation/iCTS/source/flow/FlowManager.cc` | Calls run phases, exposes `report(save_dir)`, emits key run results, and provides feature/evaluation summary output. |
| `src/operation/iCTS/api/CTSAPI.hh` | External API surface for `runCTS`, `report`, `init`, `resetAPI`, and `outputSummary`. |
| `src/operation/iCTS/api/CTSAPI.cc` | Forwards report calls to `FlowManager` and converts evaluation summary to feature summary. |
| `src/interface/tcl/tcl_icts/tcl_cts.cpp` | Tcl `cts_report` dispatch path and separate `cts_save_tree` command path. |
| `src/interface/tcl/tcl_icts/tcl_register_cts.h` | Tcl command registration for `run_cts`, `cts_report`, `cts_save_tree`, and `cts_config`. |
| `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp` | Tool-manager CTS report bridge plus legacy/tree-data save and load helpers. |
| `src/interface/python/py_icts/py_icts.cpp` | Python report bridge into `CTS_API_INST.report(path)`. |
| `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh` | Evaluation summary/statistics state types and statistics write API. |
| `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc` | Evaluation computation, summary emission, and statistics report write/rewrite entry. |
| `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.hh` | Statistics DTOs and writer facade. |
| `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc` | Writes statistics `.rpt` files and emits matching schema/log tables. |
| `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh` | Readonly CTS tree diagnostic/view model shared by report and visualization. |
| `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.cc` | String labels for net roles, inst roles, synthesis phases, sink domains, and view modes. |
| `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.hh` | Builder interface for sink-domain/source-to-root diagnostic view records. |
| `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc` | Builds routed/flyline/fallback segments and merges per-clock diagnostic views. |
| `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.hh` | Normalized visual diagnostic model consumed by SVG/GDS writers. |
| `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.cc` | Converts clock-tree view plus design fallbacks into visual diagnostic model. |
| `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.hh` | SVG visualization facade. |
| `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc` | Emits `cts_design.svg` and `cts_flyline.svg` plus schema status/diagnostic tables. |
| `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.hh` | GDS visualization facade. |
| `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc` | Emits `cts_design.gds`, `cts_flyline.gds`, and `.lyp` layer-property files plus schema status tables. |
| `src/operation/iCTS/source/flow/visualization/ClockTreeGdsWriter.hh` | Minimal GDSII file-format model and writer interface. |
| `src/operation/iCTS/source/flow/visualization/ClockTreeGdsWriter.cc` | Binary GDS and XML layer-properties writer implementation. |
| `src/operation/iCTS/source/flow/visualization/ClockTreeVisualizationLayerPolicy.hh` | Semantic layer and palette policy for visual diagnostics. |
| `src/operation/iCTS/source/utils/logger/Schema.hh` | Structured report writer interface used by runtime/stage/report code. |
| `src/operation/iCTS/source/utils/logger/Schema.cc` | Structured cts.log writer, standalone report append helpers, runtime metric tables, stage summaries, diagnostics, and artifact records. |
| `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc` | Initializes work, `visualization`, `statistics`, and `cts.log` paths, then emits runtime setup tables. |
| `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc` | Synthesis-stage summary/status tables currently emitted outside final report step. |
| `src/operation/iCTS/source/flow/synthesis/ClockSynthesisReporter.cc` | Synthesis-specific standalone/schema report output for cluster leaf distance summaries. |
| `src/operation/iCTS/source/flow/CMakeLists.txt` | Current flow subdirectory targets and dependencies. |
| `src/operation/iCTS/source/flow/stage/CMakeLists.txt` | Stage target links evaluation, netlist, and visualization privately. |
| `src/operation/iCTS/source/flow/evaluation/CMakeLists.txt` | Evaluation target owns evaluator and statistics writer. |
| `src/operation/iCTS/source/flow/clock_tree_view/CMakeLists.txt` | Clock-tree view target is separate from visualization and links database/routing privately. |
| `src/operation/iCTS/source/flow/visualization/CMakeLists.txt` | Visualization target owns SVG/GDS files and publicly consumes clock-tree view. |

### Current report call path

- External report entry exists in `CTSAPI`: `CTSAPI::report(save_dir)` forwards to `FLOW_MANAGER_INST.report(save_dir)` (`src/operation/iCTS/api/CTSAPI.hh:44`, `src/operation/iCTS/api/CTSAPI.hh:46`, `src/operation/iCTS/api/CTSAPI.cc:72`, `src/operation/iCTS/api/CTSAPI.cc:74`).
- `FlowManager` stores report inputs and state: `_run_summary`, `_clock_tree_view`, `_evaluation_state`, `_writeback_result`, `_evaluation_ready` (`src/operation/iCTS/source/flow/FlowManager.hh:68`, `src/operation/iCTS/source/flow/FlowManager.hh:73`). Its `report()` delegates to `CTSClockTreeReportStep::run(...)` with evaluation readiness, writeback state, the view, and mutable evaluation state (`src/operation/iCTS/source/flow/FlowManager.cc:120`, `src/operation/iCTS/source/flow/FlowManager.cc:124`).
- Normal `runCTS()` does not call report. It runs `readData()`, `run()`, `writeback()`, and `evaluate()`, then emits runtime summary and key results (`src/operation/iCTS/source/flow/FlowManager.cc:62`, `src/operation/iCTS/source/flow/FlowManager.cc:84`). The final report remains separately callable.
- Tcl registers `cts_report` and `cts_save_tree` as separate commands (`src/interface/tcl/tcl_icts/tcl_register_cts.h:35`, `src/interface/tcl/tcl_icts/tcl_register_cts.h:37`). `cts_report -name` first calls `iplf::tmInst->reportCTS(name)`; `cts_report -path` calls `CTS_API_INST.report(str_path)` directly (`src/interface/tcl/tcl_icts/tcl_cts.cpp:92`, `src/interface/tcl/tcl_icts/tcl_cts.cpp:104`).
- Tool-manager report bridges also end at `CTS_API_INST.report(path)` (`src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:53`, `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:59`), and the Python binding does the same (`src/interface/python/py_icts/py_icts.cpp:29`, `src/interface/python/py_icts/py_icts.cpp:31`).
- `cts_save_tree` is not the same path as `CTSAPI::report`. It calls `ToolManager::saveClockTree`, which calls `ctsInst->saveTreeDataToFile(data_path)` (`src/interface/tcl/tcl_icts/tcl_cts.cpp:111`, `src/interface/tcl/tcl_icts/tcl_cts.cpp:125`, `src/platform/tool_manager/tool_manager.cpp:344`, `src/platform/tool_manager/tool_manager.cpp:346`). That serializer builds GUI tree data from the STA clock tree and writes `CtsDbId::kCtsGuiData` (`src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:150`, `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:162`, `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:165`, `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:178`).

### Current report-step responsibilities

- `CTSClockTreeReportStep` is already more than visualization. The header names it "CTS clock-tree report step orchestration" and its result captures both report success and evaluation readiness (`src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.hh:21`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.hh:33`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.hh:45`).
- The step resolves three output roots: report root, visualization dir, and statistics dir. If `save_dir` is provided, it creates `save_dir/visualization` and `save_dir/statistics`; otherwise it reuses config/work-dir paths (`src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:43`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:70`).
- It emits a report-mode section and key-value table containing mode, save dir, visualization dir, and statistics dir (`src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:81`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:95`).
- It can reuse existing evaluation state or rebuild it inside report. The mode is `reuse_evaluation_state` when evaluation is ready and valid; otherwise it emits `### Report Evaluation` and calls the evaluation step (`src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:86`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:100`).
- It writes statistics reports, SVG visual diagnostics, and GDS visual diagnostics, then reduces those three statuses into `report_success` (`src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:103`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:107`).
- It emits a dedicated report runtime table and stage finish/fail summary with `statistics_status`, `visualization_status`, and `gds_status` (`src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:108`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:117`).

### Evaluation statistics output

- Evaluation owns computation. `ClockTreeSummary` includes final clock buffer count, buffer area, clock net wirelength, total clock tree wirelength, timing, latency, skew, and compatibility aliases (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:35`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:83`).
- `ClockTreeEvaluationState` combines `ClockTreeSummary` and `CTSStatistics`; `ClockTreeEvaluator` exposes `evaluate`, `outputSummary`, `hasEvaluationResult`, and `writeStatistics` (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:90`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:107`).
- Evaluation gathers committed CTS data from `DESIGN_INST`, `WRAPPER_INST`, and `STA_ADAPTER_INST`, computes buffer, area, RC/timing, and wirelength statistics, marks the state valid, emits the evaluation summary, and writes statistics to the configured statistics dir (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:443`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:512`).
- `ClockTreeEvaluator::writeStatistics(...)` is the reuse point used by the report step. It rejects invalid statistics, resolves a requested statistics dir or the configured dir, writes report files, and optionally emits schema/log tables (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:525`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:539`).
- `CTSStatisticsWriter` owns the concrete statistics report files: `wirelength.rpt`, `cell_stats.rpt`, and `lib_cell_dist.rpt` (`src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:131`, `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:152`). It also emits matching cts.log/schema tables when asked (`src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:155`, `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:164`).
- This means `statistics` is a real report responsibility, but metric computation should remain in `evaluation`. The report subflow should consume `ClockTreeEvaluationState` / `CTSStatistics`, not recompute them.

### Schema/log report output

- Runtime setup creates `cts.log`, `visualization`, and `statistics` paths under the work dir and stores them in `CONFIG_INST` (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:63`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:72`). It also emits `Runtime Paths`, runtime configuration, and wire-RC setup tables (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:86`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:98`).
- `SchemaWriter` is a reusable structured-output utility, not report-stage-only code. It opens report files with a run header, emits sections/tables/key-value tables/detail blocks/diagnostics/artifact records, and supports standalone append helpers (`src/operation/iCTS/source/utils/logger/Schema.hh:122`, `src/operation/iCTS/source/utils/logger/Schema.hh:151`, `src/operation/iCTS/source/utils/logger/Schema.cc:226`, `src/operation/iCTS/source/utils/logger/Schema.cc:410`).
- Runtime and stage reporting are schema concepts: runtime metrics are recorded and emitted as tables (`src/operation/iCTS/source/utils/logger/Schema.cc:485`, `src/operation/iCTS/source/utils/logger/Schema.cc:533`), and stage scopes emit start/finish/fail summaries (`src/operation/iCTS/source/utils/logger/Schema.cc:536`, `src/operation/iCTS/source/utils/logger/Schema.cc:630`).
- The schema namespace helpers dual-write to console log and schema output for tables, key-value tables, diagnostics, and artifacts (`src/operation/iCTS/source/utils/logger/Schema.cc:634`, `src/operation/iCTS/source/utils/logger/Schema.cc:668`).
- The final report architecture should not move `SchemaWriter` itself under `report`; it is cross-flow infrastructure. The report subflow should own report-specific summary/table composition and artifact status, while the schema writer stays in `utils/logger`.
- Some report-like schema output is currently emitted before final report: synthesis emits `CTS Clock Tree Synthesis Summary` and `CTS Clock Tree Sink Domains` (`src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:41`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:57`), and `ClockSynthesisReporter` appends cluster distance summaries to the active or configured report file (`src/operation/iCTS/source/flow/synthesis/ClockSynthesisReporter.cc:82`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisReporter.cc:163`). A redesign should decide whether these remain stage-owned summaries or are routed through a `report/summaries` facade.

### Clock-tree view and visual diagnostics

- `ClockTreeView` is explicitly described as a readonly view consumed by report and visualization (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:21`). It stores net roles, inst roles, synthesis phases, sink domains, view modes, segments, nets, insts, clocks, and synthesis/writeback status (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:35`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:163`).
- It has useful CTS/report vocabulary: `clock_source`, `source_to_root`, `downstream`, `sink_tree`, `logic_cell`, `clock_load`, `root_buffer`, `htree_buffer`, `source_root_buffer`, `read_data`, `downstream_htree`, `source_to_root_segment`, `source_to_root_htree`, `hard_macro`, `regular`, and `flyline` (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.cc:118`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.cc:199`).
- `ClockTreeViewBuilder` records routed tree segments, pin-to-pin flylines, fallback segments, sink insts, direct sink-domain topology, sink-domain views, source-to-root views, and merged per-clock views (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:39`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:149`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:200`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:292`).
- `ClockTreeVisualizationModelBuilder` normalizes `ClockTreeView` into a visual model and fills gaps from `DESIGN_INST` and `WRAPPER_INST`, including fallback route/flyline segments, inst geometry, logic cells, and pin markers (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.cc:330`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.cc:345`).
- The important dependency caveat: the view is not produced only by report. `FlowManager` resets and stores it (`src/operation/iCTS/source/flow/FlowManager.cc:87`, `src/operation/iCTS/source/flow/FlowManager.cc:103`), and synthesis populates and marks it complete (`src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:63`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:113`). Moving the current `ClockTreeView` wholesale into `report/` would make synthesis depend on report unless the production model is changed.
- Recommended design implication: keep stable tree facts in a neutral database/model boundary (`database/design/ClockTree` per the task direction) or leave a neutral diagnostic model outside `report` during migration. Let `report/visual_diagnostics` own conversion and format writing, not synthesis-time ownership.

### SVG/GDS artifacts

- SVG visualization emits two files: `cts_design.svg` and `cts_flyline.svg` (`src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc:49`, `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc:50`).
- SVG output writes a report status table named `CTS Visualization Reports`, and failed outputs emit `CTS Report Visualization` diagnostics (`src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc:404`, `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc:422`).
- SVG generation builds the visual model, rejects no-clock/no-net cases, writes design and flyline views, and returns aggregate success (`src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc:427`, `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc:450`).
- GDS visualization emits `cts_design.gds`, `cts_flyline.gds`, `cts_design.lyp`, and `cts_flyline.lyp` (`src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc:43`, `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc:46`).
- GDS output writes a report status table named `CTS GDS Reports` (`src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc:155`, `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc:165`), then builds design and flyline GDS libraries and writes GDS/LYP files (`src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc:170`, `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc:197`).
- `ClockTreeGdsWriter` is a format writer for paths, boundaries, texts, GDS binary, and layer properties (`src/operation/iCTS/source/flow/visualization/ClockTreeGdsWriter.hh:68`, `src/operation/iCTS/source/flow/visualization/ClockTreeGdsWriter.hh:85`). `ClockTreeVisualizationLayerPolicy` maps semantic visual concepts to GDS layers and palette properties (`src/operation/iCTS/source/flow/visualization/ClockTreeVisualizationLayerPolicy.hh:37`, `src/operation/iCTS/source/flow/visualization/ClockTreeVisualizationLayerPolicy.hh:86`).
- These files are best described as visual diagnostics, not the whole report module.

### CMake and dependency shape

- Current top-level flow exposes separate targets for `clock_tree_view`, `evaluation`, `htree`, `netlist`, `visualization`, `run_setup`, `stage`, and `synthesis` (`src/operation/iCTS/source/flow/CMakeLists.txt:1`, `src/operation/iCTS/source/flow/CMakeLists.txt:17`).
- `stage` privately links `evaluation`, `netlist`, and `visualization`; this matches the current report step living under `stage` while delegating format-specific output to visualization (`src/operation/iCTS/source/flow/stage/CMakeLists.txt:14`, `src/operation/iCTS/source/flow/stage/CMakeLists.txt:25`).
- `evaluation` currently owns both evaluation computation and statistics writer (`src/operation/iCTS/source/flow/evaluation/CMakeLists.txt:1`, `src/operation/iCTS/source/flow/evaluation/CMakeLists.txt:5`). Moving only the writer to `report/statistics` would require `evaluation` to expose `CTSStatistics` as a neutral result type and avoid linking evaluation to report.
- `clock_tree_view` is a separate target consumed publicly by visualization (`src/operation/iCTS/source/flow/clock_tree_view/CMakeLists.txt:1`, `src/operation/iCTS/source/flow/clock_tree_view/CMakeLists.txt:21`; `src/operation/iCTS/source/flow/visualization/CMakeLists.txt:10`, `src/operation/iCTS/source/flow/visualization/CMakeLists.txt:25`). This supports a migration where the model remains neutral while visual writers move under report.
- `visualization` is the current format-writer target for SVG/GDS (`src/operation/iCTS/source/flow/visualization/CMakeLists.txt:1`, `src/operation/iCTS/source/flow/visualization/CMakeLists.txt:19`).

### Proposed `report` responsibility boundary

`report` should be the readonly final-output layer for human and machine inspection of committed CTS results. It should own report orchestration, report output locations, summary/status composition, statistics file emission, generated-artifact status, and visual diagnostic artifacts. It should not own CTS synthesis decisions, instantiation/writeback mutation, evaluation metric computation, or the reusable schema writer.

Recommended responsibilities:

- Report orchestration: replace `CTSClockTreeReportStep` with a primary `report/Report.hh` and `report/Report.cc` entry that coordinates report mode, optional evaluation rebuild/reuse, statistics, visual diagnostics, artifact statuses, runtime metrics, and result state.
- Summaries: own final report-mode/run-result/status summary tables, report runtime table assembly, and final consolidated report sections. Do not move `SchemaWriter` here.
- Statistics: own file emission and schema table emission for already-computed `CTSStatistics` (`wirelength.rpt`, `cell_stats.rpt`, `lib_cell_dist.rpt`). Evaluation remains the computation owner.
- Artifacts: own report-root/dir resolution, generated-artifact status records, output manifest/status tables, and compatibility decisions around non-visual artifacts. Treat `cts_save_tree` as an external/tool-manager artifact path for now, not an immediate `CTSAPI::report` responsibility.
- Visual diagnostics: own SVG/GDS diagnostic conversion and writers for design/flyline views. Use `visual_diagnostics`, not plain `visualization`, so readers understand these are diagnostic artifacts inside a broader report layer.

### Proposed second-level folder layout

Recommended target layout:

```text
src/operation/iCTS/source/flow/report/
  Report.hh
  Report.cc

  summaries/
    ReportSummary.hh
    ReportSummary.cc
    ReportRuntime.hh
    ReportRuntime.cc

  statistics/
    StatisticsReport.hh
    StatisticsReport.cc
    StatisticsWriter.hh
    StatisticsWriter.cc

  artifacts/
    ReportPaths.hh
    ReportPaths.cc
    ArtifactManifest.hh
    ArtifactManifest.cc

  visual_diagnostics/
    VisualDiagnostics.hh
    VisualDiagnostics.cc
    ClockTreeDiagnosticModel.hh
    ClockTreeDiagnosticModel.cc
    svg/
      SvgDiagnostics.hh
      SvgDiagnostics.cc
    gds/
      GdsDiagnostics.hh
      GdsDiagnostics.cc
      GdsWriter.hh
      GdsWriter.cc
      LayerPolicy.hh
      LayerPolicy.cc
```

Meaning of each second-level folder:

| Folder | Owns | Moves from current code | Should not own |
|---|---|---|---|
| `summaries/` | Report-mode table, final report summary, runtime/status summary adapters, consolidated run/evaluation summary presentation. | Parts of `CTSClockTreeReportStep.cc`; possibly final-table formatting from `FlowManager::emitKeyResults` if design chooses final report ownership. | `SchemaWriter`, synthesis algorithms, evaluation computation. |
| `statistics/` | Statistics report-file emission and matching schema/log tables from `CTSStatistics`. | `CTSStatisticsWriter.*`, or a renamed wrapper around it. | Metric collection from `Design`, `Wrapper`, or `STAAdapter`; that remains `evaluation`. |
| `artifacts/` | Report root/path resolution, generated-file status rows, artifact manifest/result structs, user-selected save-dir policy. | Path helpers in `CTSClockTreeReportStep.cc`; status structs/tables from SVG/GDS code if generalized. | Format-specific SVG/GDS drawing; tool-manager tree-data serialization unless a later compatibility task explicitly integrates it. |
| `visual_diagnostics/` | Clock-tree diagnostic model conversion and SVG/GDS diagnostic artifact generation. | `flow/visualization/*` and the report-only visual model portions of `flow/clock_tree_view/*` once dependency direction is fixed. | General report orchestration, statistics `.rpt` files, final evaluation metric computation. |

Suggested primary class names:

| File | Primary type | Role |
|---|---|---|
| `report/Report.hh` | `CTSReport` | Main report facade replacing `CTSClockTreeReportStep`. |
| `report/summaries/ReportSummary.hh` | `ReportSummaryWriter` | Emits report-mode, final report status, and consolidated summary tables. |
| `report/summaries/ReportRuntime.hh` | `ReportRuntimeWriter` | Emits report runtime metric table and report stage status fields. |
| `report/statistics/StatisticsReport.hh` | `StatisticsReport` | Consumes `ClockTreeEvaluationState`/`CTSStatistics` and writes statistics reports. |
| `report/statistics/StatisticsWriter.hh` | `StatisticsWriter` | Lower-level `.rpt` table writer, replacing/renaming `CTSStatisticsWriter`. |
| `report/artifacts/ReportPaths.hh` | `ReportPaths` | Resolves report root, visualization dir, statistics dir, and future artifact dirs. |
| `report/artifacts/ArtifactManifest.hh` | `ArtifactManifest` | Aggregates generated/failed artifact statuses for schema/log output. |
| `report/visual_diagnostics/VisualDiagnostics.hh` | `VisualDiagnostics` | Coordinates visual diagnostic artifacts across formats. |
| `report/visual_diagnostics/ClockTreeDiagnosticModel.hh` | `ClockTreeDiagnosticModelBuilder` | Builds report-specific visual model from stable tree/design data. |
| `report/visual_diagnostics/svg/SvgDiagnostics.hh` | `SvgDiagnostics` | Emits `cts_design.svg` and `cts_flyline.svg`. |
| `report/visual_diagnostics/gds/GdsDiagnostics.hh` | `GdsDiagnostics` | Emits `cts_design.gds`, `cts_flyline.gds`, and `.lyp` files. |
| `report/visual_diagnostics/gds/GdsWriter.hh` | `GdsWriter` | Low-level GDS writer. |
| `report/visual_diagnostics/gds/LayerPolicy.hh` | `VisualDiagnosticLayerPolicy` | Semantic GDS layer/palette policy. |

### Recommended migration notes

- First extract `CTSClockTreeReportStep` into `report/Report` plus `artifacts/ReportPaths` and `summaries/ReportSummary` without changing behavior.
- Move `CTSStatisticsWriter` only after deciding whether `CTSStatistics` remains under `evaluation` as a result DTO or moves to a neutral report/evaluation contract. Avoid making `evaluation` depend on `report`.
- Move SVG/GDS writers under `report/visual_diagnostics` before moving `ClockTreeView`. The writers are report consumers and are safe to relocate conceptually.
- Treat `ClockTreeView` separately. If it remains synthesis-populated, keep it in a neutral shared location or replace it with `database/design/ClockTree` plus a report-built diagnostic model. Do not create `synthesis -> report` dependency just to make folders tidy.
- Keep public command names compatible: `cts_report` should still call `CTSAPI::report`, and `cts_save_tree` should remain a separate compatibility/export command unless a later PRD explicitly folds tree serialization into the report artifact model.

### Architecture conclusion

The proposed `report` subflow should not be `report/visualization` only. Current behavior already proves four report responsibilities:

1. Summaries and schema/log report tables: report mode, runtime/status, stage summaries, final key results.
2. Statistics reports: evaluated wirelength, cell stats, and library-cell distribution `.rpt` files.
3. Artifact management: output root/path resolution, generated-file status, diagnostics, and manifest-style reporting.
4. Visual diagnostics: SVG and GDS design/flyline views over readonly clock-tree diagnostic data.

Use `report/summaries`, `report/statistics`, `report/artifacts`, and `report/visual_diagnostics` as readable second-level folders. This makes the report layer look like a final-output subsystem, while keeping `visual_diagnostics` as one visible part of a broader report architecture.

## External references

- No new external references were used for this internal code inspection.
- Existing task research with external CTS/report terminology remains relevant: `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/industry-cts-flow-terminology.md` and `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/open-source-cts-comparison.md`.

## Related specs

- `.trellis/spec/backend/directory-structure.md`: defines iCTS source categories, flow framework, visualization-only writer placement, and CMake target expectations.
- `.trellis/spec/backend/database-guidelines.md`: says evaluation/report/visualization are readonly consumers, report-only data should be narrow and typed, visualization output roots at `visualization_dir`, and statistics output roots at `statistics_dir`.
- `.trellis/spec/backend/logging-guidelines.md`: says runtime/schema report state belongs in `SchemaWriter`, structured file output should use schema helpers, and API/flow layers coordinate output timing.
- `.trellis/spec/backend/quality-guidelines.md`: requires CTS-semantic names, visible dependencies, and avoiding broad duplicated snapshots.
- `.trellis/spec/project-constraints.md`: file naming and process constraints for future implementation.

## Caveats / Not Found

- No source code was modified. This artifact is the only intended write.
- There is no standalone `flow/report/` directory yet; report behavior is currently split across `stage/CTSClockTreeReportStep.*`, `evaluation/CTSStatisticsWriter.*`, `clock_tree_view/*`, `visualization/*`, `FlowManager`, and schema utilities.
- No current report/visualization code path directly uses `schema::EmitArtifact`; generated SVG/GDS/statistics outputs are currently reported through status tables and diagnostics.
- `cts_save_tree` is a user-visible clock-tree export command but is not part of `CTSAPI::report`; it serializes tool-manager/STA tree GUI data. Fold it into `report/artifacts` only with a separate compatibility decision.
- `ClockTreeView` currently has producer-side coupling from synthesis. Moving it under `report/visual_diagnostics` without changing the producer model would invert dependencies in the wrong direction.
