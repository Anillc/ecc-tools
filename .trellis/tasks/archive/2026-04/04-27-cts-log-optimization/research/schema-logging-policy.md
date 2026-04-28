# Research: schema logging policy

- Query: Research CTS logging schema/API patterns and duplication across `logger/Schema`, log formatting helpers, schema writer APIs, stage summaries, tests, and repeated `cts.log` tables; propose a reusable policy for narrative messages, table types, units, deduplication, and verbose/debug artifacts.
- Scope: internal
- Date: 2026-04-27

## Findings

### Files Found

- `.trellis/tasks/04-27-cts-log-optimization/prd.md` - task requirements for CTS log readability, unit policy, hierarchy, duplication, and debug-detail handling.
- `.trellis/tasks/04-27-cts-log-optimization/research/log-analysis.md` - existing observed-log analysis with suspicious metrics and improvement priorities.
- `.trellis/spec/backend/logging-guidelines.md` - current logging authority for iCTS runtime `LOG_*` and structured schema/report output.
- `.trellis/spec/project-constraints.md` - repository-level iCTS constraints, including schema/report helpers for `cts.log`.
- `scripts/design/ics55_dev/result/cts/cts.log` - generated log under analysis, 746 lines with 34 titled sections.
- `src/operation/iCTS/source/utils/logger/Schema.hh` - public schema writer API: sections, tables, key-value tables, detail blocks, diagnostics, artifacts, standalone appenders, and `ScopedStage`.
- `src/operation/iCTS/source/utils/logger/Schema.cc` - schema writer implementation and console/file dual emission helpers.
- `src/operation/iCTS/source/utils/logger/LogFormat.hh` - ASCII title/table and value-formatting helpers.
- `src/operation/iCTS/api/CTSAPI.cc` - run boundary, runtime path/config reporting, API stage timing, and schema writer lifecycle.
- `src/operation/iCTS/source/database/config/Config.cc` - runtime configuration table assembly.
- `src/operation/iCTS/source/database/design/Design.cc` - clock distribution summary table assembly.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterWireRc.cc` and `STAAdapterInternal.cc` - wire RC probe diagnostics/table rows.
- `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc` - read-data summary emission.
- `src/operation/iCTS/source/module/topology/TopologyGen.cc` - topology load distribution and root-to-leaf path summaries.
- `src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc` and `CharBuilderBuild.cc` - characterization runtime/config/grid/progress/overflow summaries.
- `src/operation/iCTS/source/flow/htree/HTreeCharacterizationFlow.cc`, `HTreeBuildSummary.cc`, `HTreeBuilder.cc`, and `HTreeLogging.cc` - H-tree grid, characterization, build, selection, and fallback reporting.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc` - clustered synthesis distance summary and full cluster detail table.
- `src/operation/iCTS/source/flow/FlowManager.cc` - CTS flow summary and sink-group table.
- `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc` - final CTS evaluation metrics and summary ownership.
- `src/operation/iCTS/test/common/logging/ScopedLogFileTest.cc`, `src/operation/iCTS/test/common/io/TestArtifactIO.cc`, `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc`, `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechArtifactAssertions.cc`, and `src/operation/iCTS/test/module/characterization/CharacterizationRealTechFallbackTest.cc` - tests and artifact helpers that constrain current log output.

### Existing Schema/API Patterns

- `SchemaWriter` exposes a small structured-output API: `emitSection`, `emitTable`, `emitKeyValueTable`, `emitDetailBlock`, `emitDiagnostic`, `emitArtifact`, plus standalone append variants for appending to a path without an active writer (`src/operation/iCTS/source/utils/logger/Schema.hh:58`, `src/operation/iCTS/source/utils/logger/Schema.hh:63`, `src/operation/iCTS/source/utils/logger/Schema.hh:71`).
- `SchemaWriter::open` truncates the target file, emits the run header, filters `generated_on`, and writes `Run Context` metadata; nested open calls suspend and later restore the previous writer (`src/operation/iCTS/source/utils/logger/Schema.cc:192`, `src/operation/iCTS/source/utils/logger/Schema.cc:210`, `src/operation/iCTS/source/utils/logger/Schema.cc:217`, `src/operation/iCTS/source/utils/logger/Schema.cc:241`).
- `schema::EmitTable`, `schema::EmitKeyValueTable`, `schema::EmitDiagnostic`, and `schema::EmitArtifact` are dual-output helpers: they write to `LOG_*` and to the active schema writer (`src/operation/iCTS/source/utils/logger/Schema.cc:421`, `src/operation/iCTS/source/utils/logger/Schema.cc:428`, `src/operation/iCTS/source/utils/logger/Schema.cc:435`, `src/operation/iCTS/source/utils/logger/Schema.cc:452`).
- `ScopedStage` emits console stage markers, optional stage context, and a final key-value summary with `outcome` and `elapsed_s` (`src/operation/iCTS/source/utils/logger/Schema.cc:373`, `src/operation/iCTS/source/utils/logger/Schema.cc:400`, `src/operation/iCTS/source/utils/logger/Schema.cc:413`). `markRunning` currently only emits console output and ignores its fields (`src/operation/iCTS/source/utils/logger/Schema.cc:390`).
- `LogFormat.hh` already centralizes ASCII rendering and basic formatting: fixed/scientific, unit suffixes, engineering prefixes, bool, percent, string joins, and stage markers (`src/operation/iCTS/source/utils/logger/LogFormat.hh:91`, `src/operation/iCTS/source/utils/logger/LogFormat.hh:107`, `src/operation/iCTS/source/utils/logger/LogFormat.hh:115`, `src/operation/iCTS/source/utils/logger/LogFormat.hh:159`, `src/operation/iCTS/source/utils/logger/LogFormat.hh:262`).
- Backend logging spec already says schema helpers own structured file output, dense summaries should use titled schema tables/detail blocks, fallback/auto-derived values must be warned and labeled, and field assembly should stay near the data owner (`.trellis/spec/backend/logging-guidelines.md:13`, `.trellis/spec/backend/logging-guidelines.md:40`, `.trellis/spec/backend/logging-guidelines.md:41`, `.trellis/spec/backend/logging-guidelines.md:42`).

### Current Emission Ownership

- API boundary owns writer lifecycle, runtime paths, top-level run stage, and manual API runtime tables (`src/operation/iCTS/api/CTSAPI.cc:75`, `src/operation/iCTS/api/CTSAPI.cc:91`, `src/operation/iCTS/api/CTSAPI.cc:105`, `src/operation/iCTS/api/CTSAPI.cc:119`, `src/operation/iCTS/api/CTSAPI.cc:147`, `src/operation/iCTS/api/CTSAPI.cc:170`, `src/operation/iCTS/api/CTSAPI.cc:176`).
- `Config` owns runtime configuration row construction and emits an `Item / Value / Detail` table (`src/operation/iCTS/source/database/config/Config.cc:270`, `src/operation/iCTS/source/database/config/Config.cc:312`).
- `STAAdapter` owns unit wire-RC reporting; the same `BuildWireRcRows` table is emitted by runtime setup and by CharBuilder initialization (`src/operation/iCTS/source/database/adapter/sta/STAAdapterWireRc.cc:61`, `src/operation/iCTS/source/database/adapter/sta/STAAdapterWireRc.cc:68`, `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc:515`).
- `Design` owns clock distribution summary after read-data (`src/operation/iCTS/source/database/design/Design.cc:452`), while `ClockNetManager` appends read-data source/count/runtime fields (`src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:305`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:315`).
- `TopologyGen` owns topology load distribution, root-to-leaf path summary, and a scoped topology build summary (`src/operation/iCTS/source/module/topology/TopologyGen.cc:136`, `src/operation/iCTS/source/module/topology/TopologyGen.cc:210`, `src/operation/iCTS/source/module/topology/TopologyGen.cc:282`, `src/operation/iCTS/source/module/topology/TopologyGen.cc:172`).
- `CharBuilder` owns runtime options, resolved initialization parameters, wire RC, sweep grids, sorted buffers, sweep progress, observed sample bounds, and scoped initialization/build summaries (`src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc:369`, `src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc:397`, `src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc:449`, `src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc:477`, `src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc:479`, `src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc:487`, `src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc:500`, `src/operation/iCTS/source/module/characterization/CharBuilderBuild.cc:58`, `src/operation/iCTS/source/module/characterization/CharBuilderBuild.cc:143`, `src/operation/iCTS/source/module/characterization/CharBuilderBuild.cc:151`, `src/operation/iCTS/source/module/characterization/CharBuilderBuild.cc:165`).
- `HTreeBuilder` owns fallback diagnostics, characterization grid planning, characterization summary, build selection summary, materialization summary, and scoped H-tree build summary (`src/operation/iCTS/source/flow/htree/HTreeLevelPlan.cc:159`, `src/operation/iCTS/source/flow/htree/HTreeCharacterizationFlow.cc:80`, `src/operation/iCTS/source/flow/htree/HTreeCharacterizationFlow.cc:121`, `src/operation/iCTS/source/flow/htree/HTreeCharacterizationFlow.cc:156`, `src/operation/iCTS/source/flow/htree/HTreeCharacterizationFlow.cc:167`, `src/operation/iCTS/source/flow/htree/HTreeBuildSummary.cc:41`, `src/operation/iCTS/source/flow/htree/HTreeBuildSummary.cc:114`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:96`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:267`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:268`).
- `ClockSynthesis` owns clustered-flow distance reporting and uses active-writer-or-standalone append logic (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:238`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:249`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:295`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:323`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:330`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:569`).
- `FlowManager` owns aggregate flow success/failure counts and sink-group table (`src/operation/iCTS/source/flow/FlowManager.cc:137`, `src/operation/iCTS/source/flow/FlowManager.cc:141`, `src/operation/iCTS/source/flow/FlowManager.cc:152`, `src/operation/iCTS/source/flow/FlowManager.cc:230`).
- `ClockTreeEvaluator` owns final writeback/timing/wirelength/buffer metrics and emits a single final evaluation table (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:217`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:234`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:292`).

### Duplication and Readability Problems

- Runtime is reported twice for each API step: a manual `CTS API * Runtime` table uses `elapsed_time_s`/`memory_delta_mb`, then the `ScopedStage` finish table repeats elapsed time as `elapsed_s` without memory (`src/operation/iCTS/api/CTSAPI.cc:96`, `src/operation/iCTS/api/CTSAPI.cc:100`, `src/operation/iCTS/api/CTSAPI.cc:110`, `src/operation/iCTS/api/CTSAPI.cc:114`, `src/operation/iCTS/api/CTSAPI.cc:124`, `src/operation/iCTS/api/CTSAPI.cc:128`; observed at `scripts/design/ics55_dev/result/cts/cts.log:79`, `scripts/design/ics55_dev/result/cts/cts.log:87`, `scripts/design/ics55_dev/result/cts/cts.log:688`, `scripts/design/ics55_dev/result/cts/cts.log:696`, `scripts/design/ics55_dev/result/cts/cts.log:720`, `scripts/design/ics55_dev/result/cts/cts.log:728`).
- Wire RC is repeated as `Runtime Routing / Wire RC` and `CharBuilder Routing / Wire RC` with identical routing layer, width, query length, resistance, capacitance, and status (`src/operation/iCTS/api/CTSAPI.cc:192`, `src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc:479`; observed at `scripts/design/ics55_dev/result/cts/cts.log:47` and `scripts/design/ics55_dev/result/cts/cts.log:188`).
- Characterization parameters are repeated across `Runtime Configuration`, `HTreeBuilder Characterization Grid Plan`, `CharBuilder Runtime Configuration`, `CharBuilder Initialization Parameters`, `CharBuilder Sweep Grids`, and `HTreeBuilder Characterization Summary` (`scripts/design/ics55_dev/result/cts/cts.log:22`, `scripts/design/ics55_dev/result/cts/cts.log:144`, `scripts/design/ics55_dev/result/cts/cts.log:159`, `scripts/design/ics55_dev/result/cts/cts.log:175`, `scripts/design/ics55_dev/result/cts/cts.log:200`, `scripts/design/ics55_dev/result/cts/cts.log:269`).
- H-tree build facts are repeated between the rich `HTreeBuilder Build Summary` and the scoped `HTreeBuilder build Summary`; both include levels, inserted insts, inserted nets, and outcome-like fields (`src/operation/iCTS/source/flow/htree/HTreeBuildSummary.cc:51`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:268`; observed at `scripts/design/ics55_dev/result/cts/cts.log:283` and `scripts/design/ics55_dev/result/cts/cts.log:318`).
- `Cluster Center vs H-Tree Leaf Distance Details` dumps all cluster rows into the main log. In the observed run the detail table starts at line 342 and dominates lines 342-666 of a 746-line log, while the summary already gives count/min/max/mean/median (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:315`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:323`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:325`; observed at `scripts/design/ics55_dev/result/cts/cts.log:331` and `scripts/design/ics55_dev/result/cts/cts.log:342`).
- Unit policy is inconsistent. Some fields embed units in the field name (`max_buf_tran_ns`, `memory_delta_mb`, `mean_distance_dbu`), some embed units in values (`0.0500 ns`, `17.2512 um`, `482876 DBU`), and evaluation wirelength values omit units entirely (`src/operation/iCTS/source/database/config/Config.cc:277`, `src/operation/iCTS/source/module/topology/TopologyGen.cc:282`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:315`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:219`; observed at `scripts/design/ics55_dev/result/cts/cts.log:716` and `scripts/design/ics55_dev/result/cts/cts.log:717`).
- Narrative is sparse. Important decisions such as auto-derived wirelength unit, characterization capping, global selection policy, overflow interpretation, and buffer-count deltas are present as table fields but not as short explanatory messages that tell a user whether action is needed (`src/operation/iCTS/source/flow/htree/HTreeCharacterizationFlow.cc:102`, `src/operation/iCTS/source/flow/htree/HTreeBuildSummary.cc:59`, `src/operation/iCTS/source/module/characterization/CharBuilderBuild.cc:151`).
- Final evaluation metrics have semantic risk. `collectBufferMembershipMetrics()` counts all clock buffer insts and assigns that count to min/max path buffer and max-level metrics, so the observed `clock_path_min_buffer`, `clock_path_max_buffer`, and `max_level_of_clock_tree` equal `buffer_num=412` rather than actual path depths (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:71`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:84`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:261`; observed at `scripts/design/ics55_dev/result/cts/cts.log:711` through `scripts/design/ics55_dev/result/cts/cts.log:715`).
- There is no production verbose/debug switch in CTS config; `Config` parses runtime/algorithm knobs but no log verbosity or detail-artifact policy (`src/operation/iCTS/source/database/config/Config.cc:250`, `src/operation/iCTS/source/database/config/Config.cc:265`, `src/operation/iCTS/source/database/config/Config.hh:113`, `src/operation/iCTS/source/database/config/Config.hh:188`).

### Existing Test and Artifact Patterns

- Tests open a per-test `cts.log` through `SCHEMA_WRITER_INST.open` and close it at test end (`src/operation/iCTS/test/main.cc:42`, `src/operation/iCTS/test/main.cc:50`, `src/operation/iCTS/test/main.cc:72`, `src/operation/iCTS/test/main.cc:78`).
- `ScopedLogFile` is a test helper around `OpenTestReport`/`CloseTestReport`, and the regression test verifies nested log redirection restores the outer destination (`src/operation/iCTS/test/common/logging/ScopedLogFile.cc:33`, `src/operation/iCTS/test/common/logging/ScopedLogFile.cc:38`, `src/operation/iCTS/test/common/logging/ScopedLogFileTest.cc:51`, `src/operation/iCTS/test/common/logging/ScopedLogFileTest.cc:74`).
- `TestArtifactIO::MirrorStandaloneTextLog` already has the right policy shape for detail suppression: main `cts.log` gets a short detail count and artifact pointer, while full details remain in the standalone artifact file (`src/operation/iCTS/test/common/io/TestArtifactIO.cc:225`, `src/operation/iCTS/test/common/io/TestArtifactIO.cc:237`, `src/operation/iCTS/test/common/io/TestArtifactIO.cc:240`).
- Existing assertions lock in current table names. For example, clustered synthesis tests expect both distance summary and distance details in `cts.log` (`src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechArtifactAssertions.cc:70`, `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechArtifactAssertions.cc:74`), H-tree smoke tests expect CharBuilder/HTree summary names and artifact-omission text (`src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:147`, `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:159`, `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:160`), and characterization fallback tests expect `CharBuilder` summary names and overflow fields (`src/operation/iCTS/test/module/characterization/CharacterizationRealTechFallbackTest.cc:119`, `src/operation/iCTS/test/module/characterization/CharacterizationRealTechFallbackTest.cc:328`).

### Proposed Reusable Policy

1. Report hierarchy:
   - Keep one top-level `Run Context` and `Runtime Paths` section at the API boundary.
   - Add one early `Runtime Setup Summary` owned by API/config/STA with configuration, effective unit-RC, and source/fallback labels. Avoid repeating the full wire-RC table inside every downstream module unless inputs differ.
   - Keep stage-specific sections in flow order: `Read Data`, `Topology`, `Characterization`, `H-Tree Selection`, `Synthesis`, `Evaluation`, `Artifacts`, `Runtime Summary`.
   - Add one final `CTS Key Results` table near the end with outcome, clocks, sinks, inserted CTS buffers, final clock buffers, selected H-tree levels/depth, max/total wirelength with units, worst cluster distance summary, major overflow counts/ratios, runtime, and memory.
   - Preserve data ownership: low-level field assembly stays near `Config`, `STAAdapter`, `Design`, `CharBuilder`, `HTreeBuilder`, `ClockSynthesis`, and `ClockTreeEvaluator`; API/flow layers only coordinate section timing and final aggregation.

2. Narrative messages:
   - Every stage should have at most one short normal narrative in the main log: "what changed, why, and whether the user should act." Prefer a key-value `Diagnostic` for fallback/warning/error decisions and a concise `DetailBlock` only when a prose explanation is clearer than fields.
   - Use `Diagnostic` for fallback and warning decisions: auto-derived wirelength unit, clamped characterization grid, zero/non-finite RC, boundary fallback, skipped clocks/groups, and suspicious evaluation metrics.
   - Use no narrative for raw successful counters unless the message adds interpretation. For example, "Characterization used 3 direct wirelength bins to cover 5 aligned topology bins; see debug artifact for full sweep details" is useful; "CharBuilder finished" is not useful because `ScopedStage` already records it.
   - For algorithm choices, include a one-line `selection_reason` or `decision` field in the primary table rather than forcing users to infer meaning from many raw fields.

3. Table types:
   - `KeyValueTable`: use for one object/stage summary where each metric appears once. Examples: runtime setup, flow summary, evaluation summary, key results.
   - `Item / Value / Detail` table: use for configuration and resolved inputs where the `Detail` column explains source, fallback, or semantic meaning.
   - Multi-row comparison table: use for bounded repeated entities that users need to compare directly. Examples: clocks, sink groups, top-N worst clusters, depth candidates if capped.
   - Detail table: do not emit unbounded row-per-cluster/row-per-pattern tables in the main log. Emit top-N plus summary in `cts.log`; write full CSV/log artifacts and register them with `EmitArtifact`.
   - Scoped stage summary: use for outcome and elapsed time only. Do not duplicate rich stage-specific metrics that are already in an owner table.

4. Unit formatting:
   - Prefer machine-stable field keys with unit suffixes for raw numeric values: `_dbu`, `_um`, `_ns`, `_pf`, `_uw`, `_mb`, `_s`.
   - Prefer value-side units for human tables only when the field/key is generic (`Value`) or when multiple rows have different units.
   - Do not mix both styles for the same metric family. For `KeyValueTable`, use unit-suffixed keys and plain formatted numeric strings; for `Item / Value / Detail`, keep units in the `Value` cell and make the `Item` a semantic name.
   - Add units to evaluation wirelength fields immediately: `max_clock_wirelength_dbu` and `total_clock_wirelength_dbu` unless a real DBU-to-um conversion is available and documented.
   - Normalize precision: runtime seconds to 3 decimals, memory MB to 1-3 decimals, ns/pF/um through `logformat::FormatWithUnit`, large DBU/integer counts as integers, percentages through `FormatPercent`, power through `FormatPowerW`.
   - Treat `dbu` as lowercase in code-facing keys and either `DBU` or `dbu` consistently in displayed values; project terminology already calls out `dbu` as the established term.

5. Deduplication:
   - Each metric should have a single owner table. Other sections may reference it by source name, derived value, or final result, but should not repeat the same full table.
   - Runtime step timing and memory should live in `SchemaWriter` runtime
     metric state. CTS call sites should request scopes with
     `SCHEMA_WRITER_INST.beginRuntimeMetric(...)`; scoped stage summaries stay
     status-only and are acquired through `SCHEMA_WRITER_INST.beginStage(...)`.
   - Wire RC should be emitted once per distinct `(routing_layer, wire_width)` probe. CharBuilder should reference `routing_rc_source=runtime_setup` when it uses the same probe, and only emit another table when it probes different parameters.
   - Characterization config should split into one resolved-options table and one sweep-results table. Remove or compress tables that only restate `max_slew`, `max_cap`, `wirelength_unit`, `iterations`, `slew_steps`, and `cap_steps`.
   - H-tree scoped summary should only include `outcome` and caller-provided
     status fields; keep selected depth, levels, inserted objects, power,
     delay, and selection policy in `HTreeBuilder Build Summary`.
   - Evaluation summary must distinguish final design metrics from HTreeBuilder-inserted metrics. Add a reconciliation table: `htree_inserted_insts`, `cluster_buffer_insts`, `pre_existing_clock_buffers`, `final_clock_buffers`, and `explanation`.

6. Optional verbose/debug artifacts:
   - Add a production policy equivalent to the existing test artifact pattern: main `cts.log` gets summary + top-N + artifact pointer; full details go to files such as `cluster_leaf_distance.csv`, `char_sweep_progress.csv`, `htree_depth_candidates.csv`, or `pareto_frontier.log`.
   - Default normal log should include: key results, all stage outcomes, configuration/resolved values, warnings/fallbacks, bounded comparison tables, and top-N outliers.
   - Verbose/debug mode should include: full row-level distance tables, all candidate/depth/frontier rows, full sweep progress, and detailed overflow samples. Until a config flag exists, full details should still be moved to artifacts and registered via `EmitArtifact` instead of main-log dumping.
   - Follow the `TestArtifactIO` wording pattern for omitted details: `detail_rows=<count>` and `details omitted from cts.log; see artifact file: <path>`.

### Suggested Migration Order

1. Introduce small reusable report helpers in `source/utils/logger`: unit/key conventions, top-N/detail-artifact emission, and optional "summary plus artifact pointer" helpers. Keep `SchemaWriter` API compatible.
2. Normalize runtime ownership by moving major-stage timing/memory into
   `SchemaWriter` runtime metrics and acquiring stage handles through
   `SCHEMA_WRITER_INST.beginStage(...)` rather than direct `schema::ScopedStage`
   construction.
3. Deduplicate wire-RC and characterization configuration tables by choosing one owner table per metric family.
4. Replace cluster leaf full detail in `cts.log` with summary + percentiles + top-N worst clusters + artifact pointer.
5. Add a final `CTS Key Results` table and evaluation metric definitions/renames, especially for path buffer counts and clock tree levels.
6. Update tests that currently assert the presence of full distance details in `cts.log`; add assertions for the artifact pointer and artifact file existence instead.

## Caveats / Not Found

- No external references were used; this research is based on repository code, Trellis specs, existing research, tests, and the generated `cts.log`.
- I did not run the CTS flow or tests; findings are static-source/log inspection only.
- I did not find a production verbose/debug log configuration knob in `Config`.
- Current tests assert some existing table names and the presence of the full cluster distance details table, so implementation will need coordinated test updates.
- The final evaluation path-depth metrics appear semantically incorrect from source inspection, but fixing that is outside this research artifact and should be handled by an implementation task.

## External References

- None.

## Related Specs

- `.trellis/spec/backend/logging-guidelines.md`
- `.trellis/spec/project-constraints.md`
- `.trellis/spec/backend/quality-guidelines.md`
