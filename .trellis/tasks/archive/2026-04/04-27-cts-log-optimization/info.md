# CTS Log Output Improvement Plan

## Current Contract

Updated on 2026-04-27 after user feedback. This file supersedes earlier planning
that proposed cluster CSV artifacts, percentile/top-worst cluster tables,
unit-suffixed user-facing field names, and narrative `Notes` detail blocks in
the main log. The latest scope also includes CTS evaluation/report parity with
`origin/cts_fix` while preserving the new structured log style.

The reference flow remains:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Reference log:

```text
scripts/design/ics55_dev/result/cts/cts.log
```

## Goals

- Keep `cts.log` readable for normal CTS users while preserving enough
  diagnostics for CTS developers.
- Use markdown-like hierarchy for the main report:
  `# iCTS Run`, `## Runtime Setup`, `### Runtime Configuration`, etc.
- Keep the main log to markdown-like hierarchy plus structured tables.
  Do not emit `Notes` titles or narrative detail blocks in the main `cts.log`.
  `Diagnostic` key-value tables remain allowed and should not be treated as
  notes.
- Use one unit style for user-facing logs:
  - default: unit in the `Value` cell;
  - exception: table headers may carry a shared unit with parentheses, such as
    `Elapsed Time (s)` or `Peak VMem Delta (MB)`;
  - main `cts.log` field names must not use unit suffixes such as `_dbu`,
    `_um`, `_um2`, `_ns`, `_pf`, `_s`, `_mb`, `_pct`, or `_mhz`.
- Keep machine feature JSON compatible with existing fields where needed.

## Schema Writer Ownership

- Runtime metric state is owned by `SchemaWriter` / `SCHEMA_WRITER_INST`.
  `CTSAPI.cc` may request scopes such as
  `SCHEMA_WRITER_INST.beginRuntimeMetric("synthesis")`, but it must not keep a
  runtime vector or delegate runtime ownership to a CTS-specific reporter.
- Stage scopes in CTSAPI and touched CTS flow/logging paths are acquired through
  `SCHEMA_WRITER_INST.beginStage(...)`. Direct stage-handle construction and
  the old public scoped-stage symbol are not part of the accepted API pattern.
- Do not introduce a CTS-specific stateful reporter under any name.
  CTS-specific result fields should stay near existing
  data owners such as `FlowManager`, `ClockTreeEvaluator`, and statistics
  writer helpers.

## Cluster Distance Policy

Do not write `cluster_leaf_distance.csv` during the normal CTS run.

Tests or scripts that assert `cluster_leaf_distance.csv` absence should use
clean output directories. The CTS runtime should not rely on hidden stale-file
cleanup side effects.

Do not emit these main-log sections or fields:

- `Cluster Center vs H-Tree Leaf Distance Details`
- `Cluster Center vs H-Tree Leaf Distance Top Worst Clusters`
- `p90_distance_*`, `p95_distance_*`, `p99_distance_*`
- `count_over_*`
- `top_worst_*`
- `detail_row_count`
- `detail_artifact`
- cluster CSV artifact pointers

Keep only compact cluster distance statistics in `cts.log`:

- `count`
- `min_distance` with `um` in the value
- `max_distance` with `um` in the value
- `mean_distance` with `um` in the value
- `median_distance` with `um` in the value

## Main Log Shape

The normal log should be organized like:

```text
# iCTS Run

## Runtime Setup
### Runtime Configuration
### Runtime Routing / Wire RC

## Input Summary
### Clock Data

## Synthesis Summary
### Topology Generation
### H-Tree Characterization
### Characterization Setup
### Characterization Results
### H-Tree Selection
### Cluster Distance Summary
### Flow Status

## Evaluation Summary
### Final Evaluation

## Runtime Summary

## Run Results
```

The exact order can follow execution order, but the hierarchy should make it
clear which sections are setup, input, synthesis decisions, evaluation, runtime,
and final results. Do not emit empty placeholder subsections; for example,
`## Synthesis Summary` should go directly to content-bearing subsections such as
`### Topology Generation`, `### H-Tree Characterization`, and
`### Characterization Setup` rather than an empty `### Synthesis Flow`.

## Notes / Detail Block Policy

- The main `cts.log` must not contain `Notes` titles, narrative note prose, or
  prose-only detail blocks such as `Runtime Setup Notes`, `ReadData Notes`,
  `Topology Notes`, `H-Tree Characterization Notes`, `CharBuilder Setup Notes`,
  `CharBuilder Results Notes`, `H-Tree Selection Notes`, `Cluster Distance
  Notes`, `Flow Status Notes`, `CTS Evaluation Notes`, `Runtime Summary Notes`,
  or `CTS Key Results Notes`.
- Keep `Diagnostic` tables. They are structured status/fallback records, not
  narrative notes.
- Exception: H-tree characterization grid fallback/adaptation is represented by
  `HTreeBuilder Characterization Grid Plan` rows. Do not emit separate
  `HTreeBuilder Diagnostic` tables for missing configured wirelength unit,
  collapsed level bins, or direct-characterization bin capping.
- Keep all business tables, key-value summaries, and bounded diagnostic tables.

## Unit Policy

Examples of expected main-log fields:

| Section | Field/Header | Value Style |
|---|---|---|
| CTS Key Results | `final_buffer_area` | `1376.480 um^2` |
| CTS Key Results | `max_clock_net_wirelength` | `289.957 um` |
| CTS Key Results | `total_clock_tree_wirelength` | `22057.109 um` |
| CTS Key Results | `elapsed_time` | `24.425 s` |
| CTS Runtime Summary | `Elapsed Time (s)` | `24.425` |
| CTS Runtime Summary | `Peak VMem Delta (MB)` | `4060.744` |
| CTS Evaluation Summary | `design_units` | `2000 DBU/um` |
| Cluster Distance Summary | `max_distance` | `64.153 um` |
| TopologyGen Load Distribution Summary | `span_width_height` | `239.668 x 240.695 um` |
| TopologyGen Load Distribution Summary | `area` | `57689.636 um^2` |
| TopologyGen Root-To-Leaf Path Summary | `max_path_length` | `293.486 um` |

Old user-facing fields such as `elapsed_time_s`, `memory_delta_mb`,
`final_buffer_area_um2`, `max_clock_net_wirelength_dbu`, and
`total_clock_tree_wirelength_dbu` must not appear in `cts.log`.

CTS-owned code, test helpers, accessors, schema fields, and JSON/config input
keys should use the single spelling `wirelength`.

## Deduplication Policy

- `Clock Distribution Summary` labels the macro/non-flop sink count as
  `Macro Sinks`; it must not use the old `Buffer Sinks` header.
- `ReadData Summary` owns clock-source/count fields only. Keep
  `clock_source`, `added_clock_nets`, and `total_clock_nets`; do not emit
  `unique_clock_domains`. Runtime and memory for read-data live in
  `CTS Runtime Summary`.
- `CTS Runtime Summary` is the only user-facing table for major stage runtime
  and memory. The rows are emitted by `SchemaWriter` from its owned runtime
  metric state.
- `Runtime Configuration` owns routing setup values such as `routing_layers`
  and `wire_width`.
- `Runtime Routing / Wire RC` owns the derived unit RC result and references
  `routing_setup_source=Runtime Configuration` instead of repeating routing
  setup values.
- CharBuilder references `routing_rc_source=Runtime Routing / Wire RC` instead
  of repeating `routing_layer`, `wire_width`, or the full RC table when it uses
  the same routing inputs.
- CharBuilder setup/result output is consolidated:
  - `CharBuilder Setup`
  - `CharBuilder Results`
  - bounded `CharBuilder Sweep Progress` may remain while small.
- `HTreeBuilder Characterization Grid Plan` is the compact owner for H-tree
  characterization fallback/adaptation. Keep the rows `source`,
  `requested_level_lengths`, `required_covering_iterations`,
  `direct_characterization_bins`, `distinct_level_bins`, and `decision_flags`
  as the main explanation.
- HTree selected solution metrics stay in `HTreeBuilder Build Summary`; scoped
  stage summary tables should stay limited to `status` plus caller-provided
  fields. Major-stage elapsed runtime and memory live only in `CTS Runtime
  Summary`.

## Evaluation Policy

- Do not show legacy ambiguous path/depth fields in `cts.log`:
  `clock_path_min_buffer`, `clock_path_max_buffer`, and
  `max_level_of_clock_tree`.
- Because the current evaluator counts clock-member buffers rather than true
  source-to-sink path depth, the main log uses:
  - `clock_member_buffer_count`
  - `path_depth_metric_status=not_reported_no_source_to_sink_traversal`
- Feature JSON can retain compatibility aliases for existing consumers.
- External feature modules are now out of scope. Restore and keep
  `src/feature/database/feature_icts.h` and
  `src/feature/parser/feature_parser_tools.cpp` unmodified; map internal CTS
  summaries only to the existing external feature fields.
- Evaluation must align with `origin/cts_fix`:
  - prepare/refresh full-design STA after CTS writeback;
  - set SDC clocks propagated before final timing evaluation;
  - build/install CTS RC trees before `updateTiming()`;
  - explicitly run `updateTiming()`, then `reportTiming()`, then query final
    timing/skew/statistics data;
  - query STA timing directly and avoid synthetic arrival fallbacks in
    evaluation metrics;
  - compute latency/skew from iSTA path data where available, following the
    reference setup/hold, worst-path, launch/capture latency, and worst-10
    average skew model.

## Statistics And Report Policy

- Generate exactly these report files under `<cts_work_dir>/statistics` during
  normal evaluation/report:
  - `wirelength.rpt`
  - `cell_stats.rpt`
  - `lib_cell_dist.rpt`
- Do not write the legacy underscored wirelength report filename or
  `net_level.rpt` for this task.
- Mirror the three report contents into `cts.log` using structured tables or
  table-equivalent blocks, with no `Notes` titles.
- Implement `CTSAPI::report(save_dir)` so report-only invocation can reuse
  existing evaluation state or rebuild the minimum required timing/statistics
  state when no reusable evaluation exists.

## Verification Plan

Run the final `ecc_dev_tools` check after the build, targeted tests, and full
iEDA CTS script pass.

Allowed checks:

- targeted CMake builds for touched iCTS test targets;
- short targeted test executables;
- the ICS55 iEDA script if runtime is acceptable.
- final `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`.

Expected verification:

- `cts.log` contains `CTS Key Results` and `CTS Runtime Summary`;
- `cts.log` contains markdown-like headings and table output;
- `cts.log` does not contain `Notes` titles, narrative note prose, or an empty
  `### Synthesis Flow` placeholder section;
- `cts.log` does not contain unit-suffixed user-facing fields;
- `cts.log` does not contain old evaluation path/depth fields;
- `cts.log` does not contain cluster CSV/top-worst/artifact pointers;
- `cts.log` does not repeat `routing_layer`, `routing_layers`, or `wire_width`
  after `Runtime Configuration`, including inside `Runtime Routing / Wire RC`
  and `CharBuilder Setup`;
- `cts.log` uses `Macro Sinks` rather than `Buffer Sinks` in
  `Clock Distribution Summary`;
- `ReadData Summary` does not contain `unique_clock_domains`;
- H-tree characterization fallback/adaptation is captured by
  `HTreeBuilder Characterization Grid Plan`, without redundant
  `HTreeBuilder Diagnostic` entries for the same grid decisions;
- clean output directories do not contain `cluster_leaf_distance.csv`;
- targeted C++ tests cover the core log contract and cluster artifact absence.
- external feature files have no diff from `HEAD`;
- `statistics/wirelength.rpt`, `statistics/cell_stats.rpt`, and
  `statistics/lib_cell_dist.rpt` exist and are non-empty;
- `cts.log` records the required statistics report contents;
- `CTSAPI::report(save_dir)` is no longer a stub and writes the required
  statistics reports.

## Out Of Scope

- Changing CTS synthesis algorithm behavior.
- Implementing true source-to-sink path/depth traversal.
- Adding a broad report tree or verbosity framework.
- Changing external feature database/parser interfaces or feature JSON schema
  in `src/feature/**`.

## Latest Verification

- `git diff --check` passed after the routing/CharBuilder/HTree selection
  deduplication cleanup.
- `cmake --build build --target icts_test_flow_manager
  icts_test_flow_synthesis icts_test_flow_htree_realtech
  icts_test_module_characterization_realtech_regression -j 8` passed.
- `./bin/icts_test_flow_manager` passed.
- `./bin/icts_test_flow_synthesis` passed.
- `./bin/icts_test_flow_htree_realtech
  --gtest_filter=HTreeBuilderRealTechSmokeTest.SynthesizesMaterializedHTreeFromRealClockLoads`
  passed.
- `./bin/icts_test_module_characterization_realtech_regression
  --gtest_filter=CharacterizationRealTechFallbackTest.WirelengthUnitFallsBackToStrongestBufferHeight:CharacterizationRealTechFallbackTest.OverflowSamplesAreSkippedAndReportedWithinLatticeBounds`
  passed.
- `cmake --build build --target iEDA -j 8` passed.
- `cd scripts/design/ics55_dev && ./iEDA -script
  ./script/iCTS_script/run_iCTS_dev.tcl` completed with `iCTS run
  successfully`.
- Final `scripts/design/ics55_dev/result/cts/cts.log` has 408 lines. It keeps
  `routing_layers` and `wire_width` only in `Runtime Configuration`, uses
  `routing_setup_source` in `Runtime Routing / Wire RC`, and uses source rows
  in `CharBuilder Setup` instead of repeating config values.
- `ecc_dev_tools` was not run for this iteration.
