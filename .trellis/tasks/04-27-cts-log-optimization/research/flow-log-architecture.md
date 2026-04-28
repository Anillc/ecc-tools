# Research: CTS flow/log architecture

- Query: Research CTS flow/log architecture for CTS log optimization, focusing on iCTS flow orchestration, stage logging, Schema/log writer usage, current `cts.log` section emission, and hierarchy problems.
- Scope: internal
- Date: 2026-04-27

## Findings

### Files Found

| File | Description |
|------|-------------|
| `scripts/design/ics55_dev/script/iCTS_script/run_iCTS_dev.tcl` | End-to-end ICS55 development flow; calls `run_cts -config ... -work_dir ./result/cts` and later emits feature/tool reports. |
| `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp` | Platform CTS bridge; resolves default config, sets status, calls `CTS_API_INST.init()` then `CTS_API_INST.runCTS()`. |
| `src/operation/iCTS/api/CTSAPI.cc` | API entry boundary; opens `cts.log`, emits run context/config/runtime tables, owns top-level `readData`, `ctsFlow`, and `evaluate` stage scopes. |
| `src/operation/iCTS/source/utils/logger/Schema.hh` | Structured report API: `SchemaWriter`, `ScopedStage`, table/detail/diagnostic/artifact helpers. |
| `src/operation/iCTS/source/utils/logger/Schema.cc` | `cts.log` writer implementation and dual console/file emission behavior. |
| `src/operation/iCTS/source/utils/logger/LogFormat.hh` | ASCII title/table and numeric/unit formatting helpers used by schema output producers. |
| `src/operation/iCTS/source/flow/FlowManager.cc` | CTS internal flow orchestration over clocks and sink groups; emits flow summary and sink-group status tables. |
| `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc` | Read-data helper; collects clock nets, emits clock distribution and read-data summaries. |
| `src/operation/iCTS/source/database/config/Config.cc` | Runtime configuration parser and report row assembly. |
| `src/operation/iCTS/source/database/adapter/sta/STAAdapterWireRc.cc` | Emits runtime/configured wire RC report tables. |
| `src/operation/iCTS/source/database/design/Design.cc` | Emits clock distribution summary. |
| `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc` | Per sink-group synthesis orchestration; optional clustering, H-tree build, and cluster-to-leaf distance tables. |
| `src/operation/iCTS/source/module/topology/TopologyGen.cc` | H-tree topology generation stage and topology summary tables. |
| `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc` | H-tree build stage orchestration and stage summary. |
| `src/operation/iCTS/source/flow/htree/HTreeCharacterizationFlow.cc` | H-tree characterization grid planning and CharBuilder result summary. |
| `src/operation/iCTS/source/flow/htree/HTreeBuildSummary.cc` | H-tree selected-topology/build decision summary assembly. |
| `src/operation/iCTS/source/flow/htree/HTreeLogging.cc` | Thin H-tree logging helper that forwards tables to schema output. |
| `src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc` | Characterization initialization/configuration reporting. |
| `src/operation/iCTS/source/module/characterization/CharBuilderBuild.cc` | Characterization sweep progress and observed-bound reporting. |
| `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc` | Final writeback/evaluation stage and CTSSummary field emission. |
| `src/feature/builder/feature_builder_tool.cpp` | Feature summary reads final CTS summary from `CTS_API_INST.outputSummary()`. |
| `src/feature/parser/feature_parser_tools.cpp` | Feature JSON serializes the same CTS evaluation fields as the final `cts.log` evaluation table. |
| `scripts/design/ics55_dev/result/cts/cts.log` | Current observed log; 746 lines, 31 titled sections, including a 319-row cluster-distance detail table. |
| `.trellis/tasks/04-27-cts-log-optimization/research/log-analysis.md` | Prior log-content analysis with suspicious metrics and priority improvement ideas. |

### Flow Orchestration

The Tcl flow calls CTS once, after DB/LEF/DEF initialization and before DEF/netlist/report output. In the development script, `TOOL_REPORT_DIR` is `./result/cts`, and `run_cts -config $IEDA_CONFIG_DIR/cts_default_config.json -work_dir $TOOL_REPORT_DIR` is the direct producer of `result/cts/cts.log` (`run_iCTS_dev.tcl:14`, `run_iCTS_dev.tcl:68`). The script then saves DEF/netlist, emits feature summaries, and calls `cts_report -path $RESULT_DIR/cts`; `CTSAPI::report()` is currently a stub, so the meaningful report file is produced during `run_cts`, not during `cts_report` (`run_iCTS_dev.tcl:73`, `run_iCTS_dev.tcl:84`, `run_iCTS_dev.tcl:91`; `CTSAPI.cc:133`).

The platform bridge is thin. `CtsIO::runCTS()` resolves an empty config from flow config, sets the status stage, calls `CTS_API_INST.init(config, work_dir)`, then `CTS_API_INST.runCTS()` (`icts_io.cpp:33`, `icts_io.cpp:40`, `icts_io.cpp:44`, `icts_io.cpp:45`). `ToolManager::autoRunCTS()` forwards to that bridge (`tool_manager.cpp:269`).

`CTSAPI::init()` is the report boundary. It resets CTS state, parses config, resolves/creates the work directory, sets `cts.log`, GDS, and output DEF paths, opens `SCHEMA_WRITER_INST` with run metadata, emits runtime paths, initializes Wrapper and STA, then emits runtime config and configured wire RC tables (`CTSAPI.cc:147`, `CTSAPI.cc:149`, `CTSAPI.cc:160`, `CTSAPI.cc:170`, `CTSAPI.cc:176`, `CTSAPI.cc:186`, `CTSAPI.cc:189`, `CTSAPI.cc:191`, `CTSAPI.cc:192`).

`CTSAPI::runCTS()` wraps the main run in one `ScopedStage("CTS", "Clock tree synthesis API flow")`, then invokes `readData()`, `ctsFlow()`, and `evaluate()` in order, and finally emits the top-level runtime/memory summary (`CTSAPI.cc:75`, `CTSAPI.cc:78`, `CTSAPI.cc:79`, `CTSAPI.cc:80`, `CTSAPI.cc:81`, `CTSAPI.cc:84`). The three sub-steps also create `ScopedStage` objects and emit separate API runtime tables (`CTSAPI.cc:91`, `CTSAPI.cc:96`, `CTSAPI.cc:105`, `CTSAPI.cc:110`, `CTSAPI.cc:119`, `CTSAPI.cc:124`).

`FlowManager::readData()` resets evaluation summary state and delegates to `ClockNetManager::readClockData()` (`FlowManager.cc:224`). `FlowManager::run()` loops over clocks, partitions sinks into hard-macro and regular groups, inserts a root buffer per non-empty group, calls `ClockSynthesis::build()` for groups with at least two sinks, commits inserted objects, and emits the final flow summary (`FlowManager.cc:230`, `FlowManager.cc:243`, `FlowManager.cc:252`, `FlowManager.cc:264`). `FlowManager::evaluate()` delegates to `ClockTreeEvaluator::evaluate()` (`FlowManager.cc:268`).

For each non-trivial sink group, `ClockSynthesis::build()` optionally runs sink clustering, materializes cluster buffers, reconnects the root net to H-tree sinks, runs `HTreeBuilder::build()`, absorbs H-tree objects, emits cluster-to-leaf distance tables if clustering is enabled, and marks the result successful (`ClockSynthesis.cc:521`, `ClockSynthesis.cc:528`, `ClockSynthesis.cc:540`, `ClockSynthesis.cc:545`, `ClockSynthesis.cc:550`, `ClockSynthesis.cc:551`, `ClockSynthesis.cc:560`, `ClockSynthesis.cc:569`, `ClockSynthesis.cc:570`, `ClockSynthesis.cc:572`).

### Schema And Stage Logging Pattern

`SchemaWriter` exposes only flat block primitives: section, table, key-value table, detail block, diagnostic, artifact, and standalone append helpers (`Schema.hh:58`, `Schema.hh:63`, `Schema.hh:64`, `Schema.hh:65`, `Schema.hh:66`, `Schema.hh:67`, `Schema.hh:69`, `Schema.hh:71`). There is no explicit report tree, section stack, severity filter, verbosity level, or table category in the writer API.

`SchemaWriter::open()` truncates/opens the report, writes a generated timestamp, and emits `Run Context` from metadata (`Schema.cc:192`, `Schema.cc:204`, `Schema.cc:210`, `Schema.cc:213`). Each subsequent block is appended with a blank line, preserving call order but not structural parentage (`Schema.cc:277`, `Schema.cc:282`, `Schema.cc:285`, `Schema.cc:289`).

The free functions `schema::EmitTable()` and `schema::EmitKeyValueTable()` dual-write: they log the formatted table to `LOG_INFO` and then write the same table to `SCHEMA_WRITER_INST` (`Schema.cc:421`, `Schema.cc:423`, `Schema.cc:425`, `Schema.cc:428`, `Schema.cc:430`, `Schema.cc:432`). This explains why cts.log is almost entirely tables: the schema primitive produces a full titled table for every structured output call.

`ScopedStage` is mostly a console progress helper plus a final schema summary. Stage construction emits `LOG_INFO` stage markers and optionally a context table if start fields exist (`Schema.cc:373`, `Schema.cc:376`, `Schema.cc:379`). `markRunning()` emits only a console marker and discards its `fields` argument (`Schema.cc:390`, `Schema.cc:393`, `Schema.cc:397`). `finish()` emits a key-value summary table titled `<module> <stage> Summary` (`Schema.cc:400`, `Schema.cc:407`, `Schema.cc:413`). Therefore, the current `cts.log` contains stage end summaries but not stage start/running hierarchy or nested stage boundaries.

Diagnostics are key-value tables named `<owner> Diagnostic`; diagnostics carry severity/owner/summary fields but still appear as standalone peer sections in the flat log (`Schema.cc:99`, `Schema.cc:121`, `Schema.cc:317`, `Schema.cc:320`, `Schema.cc:435`, `Schema.cc:449`).

`LogFormat` centralizes table rendering and some unit formatting (`LogFormat.hh:75`, `LogFormat.hh:91`, `LogFormat.hh:115`, `LogFormat.hh:130`, `LogFormat.hh:207`, `LogFormat.hh:241`, `LogFormat.hh:251`). Unit usage is currently producer-driven, not schema-driven: producers decide whether units go in field names, values, or details.

### Current cts.log Sections And Emit Owners

Observed `cts.log` has 746 lines and 31 titled sections (`cts.log:1`, `cts.log:746`). Its section order is chronological call order, not user-facing hierarchy.

| Log section(s) | Current owner / source |
|----------------|------------------------|
| `Run Context`, `Runtime Paths` | `SchemaWriter::open()` metadata and `CTSAPI::init()` path table (`Schema.cc:213`; `CTSAPI.cc:170`, `CTSAPI.cc:176`). |
| `Runtime Configuration` | `Config::emitRuntimeConfigReport()` builds runtime config rows and emits an item/value/detail table (`Config.cc:270`, `Config.cc:312`). |
| `Runtime Routing / Wire RC` | `STAAdapter::emitConfiguredUnitWireRcReport()` probes effective RC and emits a table (`STAAdapterWireRc.cc:61`, `STAAdapterWireRc.cc:68`). |
| `Clock Distribution Summary` | `Design::emitClockDistributionSummary()` groups clocks and emits clock/sink counters (`Design.cc:452`, `Design.cc:498`). |
| `ReadData Summary` | `ClockNetManager::readClockData()` emits clock source, clock counts, runtime, and memory (`ClockNetManager.cc:286`, `ClockNetManager.cc:315`). |
| `CTS API ReadData Runtime`, `CTSReadData ... Summary` | `CTSAPI::readData()` emits API runtime table; `ScopedStage::finish()` emits stage summary (`CTSAPI.cc:91`, `CTSAPI.cc:96`, `CTSAPI.cc:100`; `Schema.cc:413`). |
| `TopologyGen Load Distribution Summary`, `TopologyGen Root-To-Leaf Path Summary`, `TopologyGen Build ... Summary` | `TopologyGen::build()` scoped stage plus `reportLoadDistribution()` and `reportRootToLeafLengths()` (`TopologyGen.cc:136`, `TopologyGen.cc:143`, `TopologyGen.cc:171`, `TopologyGen.cc:172`, `TopologyGen.cc:210`, `TopologyGen.cc:282`). |
| `HTreeBuilder Diagnostic` | HTree fallback/selection diagnostics via `schema::EmitDiagnostic()` (`HTreeBuilder.cc:218`) and grid fallback paths from level-plan code (`HTreeLevelPlan.cc:159`, `HTreeLevelPlan.cc:167`). |
| `HTreeBuilder Characterization Grid Plan` | `RunCharacterizationFlow()` builds grid decision rows and emits them through `LogInfoTable()` (`HTreeCharacterizationFlow.cc:80`, `HTreeCharacterizationFlow.cc:102`, `HTreeCharacterizationFlow.cc:121`). |
| `CharBuilder Runtime Configuration`, `CharBuilder Initialization Parameters`, `CharBuilder Routing / Wire RC`, `CharBuilder Sweep Grids`, `CharBuilder Sorted Buffers`, `CharBuilder initialization Summary` | `CharBuilder::init()` and helpers (`CharBuilderConfig.cc:369`, `CharBuilderConfig.cc:397`, `CharBuilderConfig.cc:449`, `CharBuilderConfig.cc:477`, `CharBuilderConfig.cc:479`, `CharBuilderConfig.cc:487`, `CharBuilderConfig.cc:499`). |
| `CharBuilder Sweep Progress`, `CharBuilder Observed Sample Bounds`, `CharBuilder build Summary` | `CharBuilder::build()` progress rows, observed fields, and stage finish (`CharBuilderBuild.cc:56`, `CharBuilderBuild.cc:143`, `CharBuilderBuild.cc:151`, `CharBuilderBuild.cc:163`, `CharBuilderBuild.cc:165`). |
| `HTreeBuilder Characterization Summary` | `RunCharacterizationFlow()` after CharBuilder completes (`HTreeCharacterizationFlow.cc:156`, `HTreeCharacterizationFlow.cc:167`). |
| `HTreeBuilder Build Summary` | `LogHTreeBuildSummary()` emits selected-depth/topology/frontier/metrics table (`HTreeBuildSummary.cc:41`, `HTreeBuildSummary.cc:51`, `HTreeBuildSummary.cc:114`). |
| `HTreeBuilder build Summary` | `HTreeBuilder::build()` final `ScopedStage::finish()` (`HTreeBuilder.cc:267`, `HTreeBuilder.cc:268`). |
| `Cluster Center vs H-Tree Leaf Distance Summary`, `... Details` | `ClockSynthesis::emitClusterLeafDistanceTables()` emits both the summary and full per-cluster table to the active report (`ClockSynthesis.cc:249`, `ClockSynthesis.cc:315`, `ClockSynthesis.cc:323`, `ClockSynthesis.cc:325`). |
| `CTS Flow Summary`, `CTS Flow Sink Groups` | `FlowManager::emitFlowSummary()` (`FlowManager.cc:137`, `FlowManager.cc:141`, `FlowManager.cc:153`). |
| `CTS API Flow Runtime`, `CTSFlow ... Summary` | `CTSAPI::ctsFlow()` and `ScopedStage::finish()` (`CTSAPI.cc:105`, `CTSAPI.cc:110`, `CTSAPI.cc:114`; `Schema.cc:413`). |
| `CTS Evaluation Summary` | `ClockTreeEvaluator::emitEvaluationSummary()` (`ClockTreeEvaluator.cc:217`, `ClockTreeEvaluator.cc:219`). |
| `CTS API Evaluation Runtime`, `CTSEvaluation ... Summary`, `CTS Clock tree synthesis API flow Summary` | `CTSAPI::evaluate()` and `CTSAPI::runCTS()` stage summaries (`CTSAPI.cc:119`, `CTSAPI.cc:124`, `CTSAPI.cc:128`, `CTSAPI.cc:75`, `CTSAPI.cc:84`). |

### Hierarchy Problems

The report has a single flat append stream, so unrelated audiences are intermixed. Run context/configuration, flow status, topology decisions, characterization debug details, quality/evaluation metrics, and API runtime are all sibling sections. In the observed log, run context/config is lines 4-57, read-data status is lines 59-94, topology/algorithm characterization is lines 96-330, cluster debug detail dominates lines 331-665, flow status is lines 667-703, evaluation is lines 705-735, and final API runtime is lines 737-746 (`cts.log:4`, `cts.log:59`, `cts.log:96`, `cts.log:331`, `cts.log:667`, `cts.log:705`, `cts.log:737`).

Stage names encode hierarchy textually but do not create hierarchy. For example, `CTSFlow Run CTS synthesis flow Summary` is a sibling table after `CTS Flow Summary` and `CTS Flow Sink Groups`, while the large H-tree/CharBuilder section appears before the high-level CTS flow summary. That ordering is accurate execution order, but it hides the user-facing question: did CTS succeed, what was inserted, and what quality resulted?

There is duplicate runtime/state information. API methods emit explicit runtime tables and `ScopedStage::finish()` emits elapsed summaries for the same scopes (`CTSAPI.cc:96`, `CTSAPI.cc:100`, `CTSAPI.cc:110`, `CTSAPI.cc:114`, `CTSAPI.cc:124`, `CTSAPI.cc:128`). Top-level `runCTS()` also emits both `elapsed_time_s` and `elapsed_s` in the final stage summary (`CTSAPI.cc:84`, `Schema.cc:409`).

Configuration and algorithm decisions overlap but are not linked. `Runtime Configuration` reports `wirelength_unit = auto` (`cts.log:31`), then `HTreeBuilder Diagnostic` reports a fallback and effective unit (`cts.log:133`, `cts.log:140`), then `HTreeBuilder Characterization Grid Plan` and CharBuilder tables repeat wirelength unit/iterations in several forms (`cts.log:144`, `cts.log:159`, `cts.log:175`, `cts.log:269`). The data is useful, but the log does not separate "configured input", "resolved/derived decision", and "debug inputs to CharBuilder".

The largest detail table is debug-level but emitted inline. `Cluster Center vs H-Tree Leaf Distance Details` starts at line 342 and runs through line 665, roughly 43% of the file (`cts.log:342`, `cts.log:665`). The summary immediately above is compact (`cts.log:331`), so the detail table should likely become a debug artifact or top-N outlier table in the main log. Existing test support already uses a pattern that mirrors detail files into `cts.log` as a short "details omitted from cts.log; see artifact file" detail block (`TestArtifactIO.cc:225`, `TestArtifactIO.cc:238`, `TestArtifactIO.cc:240`), and Schema already supports `EmitArtifact()` (`Schema.hh:123`, `Schema.cc:452`).

Quality/evaluation metrics are currently final but under-defined. `ClockTreeEvaluator::collectBufferMembershipMetrics()` counts all buffer member insts and assigns that same count into min/max path buffer and max level metrics (`ClockTreeEvaluator.cc:71`, `ClockTreeEvaluator.cc:84`, `ClockTreeEvaluator.cc:85`, `ClockTreeEvaluator.cc:86`). The emitted evaluation table then labels these as `clock_path_min_buffer`, `clock_path_max_buffer`, and `max_level_of_clock_tree` (`ClockTreeEvaluator.cc:219`, `ClockTreeEvaluator.cc:224`, `ClockTreeEvaluator.cc:225`, `ClockTreeEvaluator.cc:226`), which explains the observed `412`/`412`/`412` values despite HTreeBuilder selecting 6 levels (`cts.log:711`, `cts.log:713`, `cts.log:715`; `cts.log:287`). These fields also feed feature JSON unchanged (`feature_builder_tool.cpp:57`, `feature_builder_tool.cpp:59`; `feature_parser_tools.cpp:292`, `feature_parser_tools.cpp:298`).

Unit policy is inconsistent because units are string-formatted by each owner. Config and H-tree tables put units in values (`Config.cc:278`, `HTreeBuildSummary.cc:80`, `HTreeBuildSummary.cc:81`), cluster distance summary puts DBU in field names (`ClockSynthesis.cc:315`), evaluation wirelength fields have no unit in either field name or value (`ClockTreeEvaluator.cc:227`, `ClockTreeEvaluator.cc:228`), and runtime/memory are field-name units (`CTSAPI.cc:96`, `CTSAPI.cc:110`, `CTSAPI.cc:124`).

### Recommended Report Hierarchy

Use a small, stable hierarchy in `cts.log`, with execution detail moved under explicit subsections or artifacts:

1. **Run Context**
   - Invocation/config/work/output paths.
   - Version/build info if available later.
   - No algorithm details.

2. **Configuration**
   - User config values.
   - Resolved defaults/fallbacks as a separate `Resolved Configuration` or `Configuration Decisions` table.
   - Runtime RC/unit metadata.
   - Avoid repeating the same resolved unit in every downstream table unless it is needed for interpretation.

3. **Flow Progress**
   - Read-data summary.
   - Per-clock/per-sink-group status.
   - Inserted object counts.
   - One consolidated runtime table for `read_data`, `synthesis`, `evaluation`, and `total`.

4. **Algorithm Decisions**
   - Topology load distribution/root-to-leaf summary.
   - H-tree characterization grid decision.
   - Selected H-tree depth/pattern/frontier metrics.
   - Key fallback diagnostics with severity.
   - Keep detailed frontier/CharBuilder internals out of the normal path unless debug mode is enabled.

5. **Quality / Evaluation**
   - Buffer count reconciliation: root sink-group buffers, cluster buffers, H-tree buffers, final evaluation buffers.
   - Wirelength with explicit units.
   - Path-depth metrics only if computed as real source-to-sink path metrics; otherwise rename current fields to membership totals.
   - Timing summary when available.
   - Quality flags for characterization overflows and cluster distance outliers.

6. **Debug Details / Artifacts**
   - Full cluster-to-leaf details as CSV or text artifact, with top-N worst clusters and percentiles in the main log.
   - CharBuilder sweep progress can stay in main log only if considered normal-user useful; otherwise summarize with total samples, overflow ratios, and max observed bounds, then artifact detailed per-wirelength rows.
   - H-tree frontier/selection internals should stay in existing report artifacts where possible; test support already expects "details omitted" for such detailed reports (`HTreeBuilderRealTechSmokeTest.cc:159`, `HTreeBuilderRealTechSmokeTest.cc:161`).

### Practical Ownership Model

- `CTSAPI` should remain the report lifecycle owner: open/close `SchemaWriter`, emit run context, and coordinate top-level stage/runtime summaries (`CTSAPI.cc:170`, `CTSAPI.cc:144`).
- `FlowManager` should own flow progress summaries because it sees clocks, sink groups, success/skip/failure status, and final sink-group rows (`FlowManager.cc:137`, `FlowManager.cc:157`, `FlowManager.cc:261`).
- Data/config owners should assemble their own fields per spec: `Config` for runtime config, `STAAdapter` for RC/unit probes, `Design`/`ClockNetManager` for clock distribution/read-data (`Config.cc:312`, `STAAdapterWireRc.cc:61`, `Design.cc:452`, `ClockNetManager.cc:315`).
- Algorithm modules should own algorithm decision fields near the data owner: `TopologyGen` for load/path topology summaries, `HTreeBuilder`/`HTreeCharacterizationFlow` for H-tree grid/depth/selection decisions, `CharBuilder` for characterization sampling summaries (`TopologyGen.cc:210`, `HTreeCharacterizationFlow.cc:121`, `HTreeBuildSummary.cc:114`, `CharBuilderBuild.cc:143`).
- `ClockTreeEvaluator` should own final quality/evaluation metrics, but current path/depth names need correction or clearer definitions before they are elevated as user-facing quality indicators (`ClockTreeEvaluator.cc:71`, `ClockTreeEvaluator.cc:217`).
- `SchemaWriter` needs either a hierarchical section primitive or a higher-level report composer. Without a new grouping API, producers can only mimic hierarchy via title prefixes, which caused the current flat table stream (`Schema.hh:63`, `Schema.hh:64`, `Schema.hh:65`).

## Code Patterns

- **Flat schema append pattern:** `SchemaWriter::emitTable()` and `emitKeyValueTable()` append one formatted block each; no parent section state is tracked (`Schema.cc:299`, `Schema.cc:305`).
- **Dual console/file pattern:** `schema::EmitTable()` and `schema::EmitKeyValueTable()` emit the same formatted table to both `LOG_INFO` and `SCHEMA_WRITER_INST` (`Schema.cc:421`, `Schema.cc:428`).
- **Stage summary pattern:** `ScopedStage::finish()` adds `outcome` and `elapsed_s`, then emits `<module> <stage> Summary` as a key-value table (`Schema.cc:407`, `Schema.cc:409`, `Schema.cc:413`).
- **Runtime report duplication pattern:** `CTSAPI::readData()`, `ctsFlow()`, and `evaluate()` each emit an API runtime table and then the stage scope emits a second elapsed table (`CTSAPI.cc:96`, `CTSAPI.cc:100`, `CTSAPI.cc:110`, `CTSAPI.cc:114`, `CTSAPI.cc:124`, `CTSAPI.cc:128`).
- **Local field assembly pattern:** config rows are built inside `Config`, wire RC rows inside STA adapter internals, and H-tree rows near H-tree data owners, matching the logging spec's "build schema fields near the data owner" rule (`Config.cc:270`, `STAAdapterWireRc.cc:61`, `HTreeBuildSummary.cc:41`).
- **Large-detail inline pattern:** cluster-to-leaf distance emits both summary and full detail table to the active writer unconditionally when clustering is enabled (`ClockSynthesis.cc:323`, `ClockSynthesis.cc:325`).
- **Artifact reference pattern exists:** `schema::EmitArtifact()` emits a `Generated Artifact` table, and test utilities already summarize omitted detail artifacts in `cts.log` (`Schema.cc:452`, `TestArtifactIO.cc:238`, `TestArtifactIO.cc:240`).
- **Evaluation metric naming risk:** buffer membership count is used as min path buffer, max path buffer, and max level, so the current names imply path/depth semantics the implementation does not compute (`ClockTreeEvaluator.cc:71`, `ClockTreeEvaluator.cc:84`, `ClockTreeEvaluator.cc:86`, `ClockTreeEvaluator.cc:224`, `ClockTreeEvaluator.cc:226`).

## External References

- No external references were needed. This research is based on local repository code, the checked-in Trellis specs, and the generated ICS55 `cts.log`.

## Related Specs

- `.trellis/spec/backend/index.md`: backend spec index for `src/operation/iCTS/`.
- `.trellis/spec/project-constraints.md`: requires repository `LOG_*` macros for runtime logging and iCTS schema/report helpers for structured file output such as `cts.log`.
- `.trellis/spec/backend/logging-guidelines.md`: says API/flow entry layers coordinate stage boundaries and output timing, while low-level field assembly belongs near data owners; also prefers titled schema tables/detail blocks for dense summaries.
- `.trellis/spec/backend/directory-structure.md`: clarifies API/source/test layer boundaries and `source/flow` as the orchestration category.
- `.trellis/tasks/04-27-cts-log-optimization/prd.md`: user-identified problems: table-heavy output, inconsistent units, oversized detail tables, lack of hierarchy, repeated fields.
- `.trellis/tasks/04-27-cts-log-optimization/research/log-analysis.md`: prior content analysis of the observed `cts.log`, including suspicious evaluation metrics and cluster-distance outlier statistics.

## Caveats / Not Found

- I did not rerun the full iCTS flow; findings use the existing `scripts/design/ics55_dev/result/cts/cts.log` generated at `2026-04-27 12:03:14`.
- I did not edit code or task metadata. This artifact is the only file written for this research request.
- I did not find a schema-level hierarchy, verbosity, or category API. Current hierarchy can only be simulated with titles unless `SchemaWriter` gains new primitives or a report composer is introduced.
- I did not find an implemented `CTSAPI::report()` path; `cts_report` currently calls a stub, so `cts.log` structure is controlled by the run-time emit calls during `run_cts`.
- I did not find production code that already writes full cluster-distance details to a separate artifact; only test/report helper patterns show how omitted details can be referenced from `cts.log`.
