# Current CTS Code Map

This note captures the starting point for the cleanup task. It is a handoff aid for implement/check agents; the PRD remains the source of truth for scope.

## Synthesis Transaction Hotspots

- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.hh`
  - Exposes one static `run(...)` entry that receives clock, report data, run summary, schema rows, and counters.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc`
  - Driver-local `SinkDomainContext` owns sink-domain role, object prefix, sinks, root buffer pins, and downstream net.
  - Driver-local helpers currently perform:
    - flow row construction,
    - rollback and clock membership clearing,
    - sink-domain root-buffer insertion,
    - downstream-net connection,
    - downstream synthesis,
    - inserted-object commit,
    - source-to-root synthesis,
    - summary counter updates.
  - This is the primary extraction target for `ClockSinkDomainBuilder`, `ClockTreeSynthesisTransaction`, and a flow-row/status table helper.

## Result Layering Hotspots

- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh`
  - `ClockSynthesis::BuildResult` mixes:
    - algorithm result: H-tree result, cluster result, selected depth/level counters,
    - diagnostics: success/failure and failure reason,
    - materialized objects: inserted insts/pins/nets,
    - report metadata: inserted inst/net topology levels and selected H-tree counters.
  - `ClockSynthesis::SourceToRootBuildResult` has the same broad shape.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh`
  - `HTreeBuilder::BuildResult` is wider: topology, level plans, characterization results, diagnostics/fallback, materialized objects, inserted levels, and root object pointers.
- `src/operation/iCTS/source/flow/report_data/ClockTreeReportDataBuilder.hh`
  - Publicly includes `synthesis/ClockSynthesis.hh`.
  - Public report-data builder methods consume `ClockSynthesis::BuildResult` and `ClockSynthesis::SourceToRootBuildResult` directly.
  - The cleanup target is a narrow report input/view so report-data construction no longer exposes the full synthesis result contract.

## Visualization Hotspots

- `src/operation/iCTS/source/flow/report/CTSVisualizationReport.cc`
  - SVG path currently owns model/fallback/style decisions for SVG output.
- `src/operation/iCTS/source/flow/report/CTSGdsReport.cc`
  - GDS path currently owns parallel layer/model/fallback decisions for GDS output.
- `src/operation/iCTS/source/flow/report_data/ClockTreeReportData.hh`
  - Already carries typed report metadata for clocks, insts, nets, segments, sink domain, synthesis phase, net role, and route/flyline roles.
  - This should feed a shared normalized `ClockTreeVisualizationModel`.

## Evaluation Hotspots

- `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh`
  - Static evaluator facade exposes `evaluate()`, `outputSummary()`, `hasEvaluationResult()`, `writeStatistics(...)`, and `resetSummary()`.
- `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc`
  - `latestSummary()` and `latestStatistics()` are static function-local storage.
  - Report flow currently checks/reuses this hidden latest state.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc`
  - Report step resolves visualization/statistics dirs, checks evaluation readiness, writes statistics, then emits SVG/GDS.
  - It should explicitly hold/pass evaluation state after sessionization.

## Config and Facade Hotspots

- `src/operation/iCTS/source/database/config/Config.hh`
  - `max_length` remains as a legacy compatibility knob.
  - `visualization_dir` and `statistics_dir` are already present.
- `src/operation/iCTS/source/database/config/Config.cc`
  - `max_length` is still parsed and logged as legacy compatibility.
- `src/operation/iCTS/source/flow/session/CTSRunEnvironment.cc`
  - Initializes `visualization_dir` and `statistics_dir` from the runtime work directory and logs them.
- `src/operation/iCTS/source/flow/FlowManager.hh`
  - Internal singleton facade still uses a generic name and macro `FLOW_MANAGER_INST`.
  - Rename only after evaluating source/test caller churn.

## Follow-up Naming and GDS Notes - 2026-04-29

- GDS visualization now has a view-specific LYP contract: `visualization/gds/cts_design.gds` pairs with `visualization/gds/cts_design.lyp`, and `visualization/gds/cts_flyline.gds` pairs with `visualization/gds/cts_flyline.lyp`.
- `source/flow/session/` should become `source/flow/run_setup/` in a dedicated migration. The directory owns CTS run setup, not a broad session abstraction.
- `source/flow/report_data/` should become `source/flow/clock_tree_view/` in a dedicated migration. It is the typed readonly clock-tree view shared by synthesis adapters, netlist helpers, flow state, reports, and visualization.
- `source/flow/report/` should become `source/flow/visualization/` if textual statistics continue to live outside that directory. Its current files are SVG/GDS visualization writers plus GDS layer policy.
- Directory renames are deferred from the LYP follow-up because the include/CMake blast radius crosses source, API, tests, CMake target names, backend specs, and existing task notes.

## Existing Tests To Extend

- `src/operation/iCTS/test/flow/FlowManagerTest.cc`
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc`
- `src/operation/iCTS/test/flow/synthesis/*`
- `src/operation/iCTS/test/flow/htree/*`
- Add mirrored tests under `test/flow/stage/`, `test/flow/report_data/`, `test/flow/report/`, or `test/flow/evaluation/` as the new files are extracted.
