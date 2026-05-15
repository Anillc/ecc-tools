# Implementation Plan

## Phase 0: Baseline

- Save a baseline copy or metrics snapshot of `scripts/design/ics55_huge_dev/result/cts/cts.log`.
- Add a small scriptable check or test helper that counts repeated titles in a generated log.
- Confirm the current dev command still runs before report changes if runtime permits:
  `cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_huge_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`

## Phase 1: Report Policy Infrastructure

- Add dual structured report sinks to `SchemaWriter`: default `cts.log` and detail `cts_detail.log`.
- Add report routing primitives so each emitted section/table can target default, detail, or both.
- Add `StageReportOptions` or equivalent to control whether stage Context/Summary tables are written to `cts.log`, `cts_detail.log`, both, or neither.
- Preserve console lifecycle markers.
- Ensure failure/skipped stage summaries and diagnostics remain visible in default `cts.log`.
- Route successful helper-stage lifecycle detail to `cts_detail.log` when useful; suppress it entirely when it adds only `status=finished`.
- Add focused tests for:
  - default/detail routing
  - suppressed trivial finished summaries in `cts.log`
  - failure summary still emitted
  - detail file generation

## Phase 2: Collapse HTree Depth Search Output

- Extend depth-search result data structures to retain per-depth report rows.
- Emit one default `HTree Depth Candidate Summary` table from the HTree owner after depth search.
- Mark internal per-depth `HTreeDepth Build pattern frontier`, `Apply root-driver compensation`, and `Filter sink-load region` Context/Summary tables as detail-only or replace them with curated detail tables in `cts_detail.log`.
- Keep detailed compensation counters in `cts_detail.log`.
- Assert that default `cts.log` no longer contains repeated per-depth `HTreeDepth ... Context/Summary` table pairs.

## Phase 3: Remove Repeated HTree Scope Context

- Add one HTree scope table for clock/net/domain/stage/object prefix/topology context.
- Remove repeated scope rows from characterization and selection tables where the same information is already visible.
- Keep failure diagnostics local and explicit.

## Phase 4: Split Wide HTree Selection Details

- Make default `HTree Synthesis Overview` concise.
- Move long per-level and bucket-detail fields to `cts_detail.log`.
- Add a default report reference to `cts_detail.log` in the artifact/report overview.

## Phase 5: Characterization and Source Trunk Cleanup

- Rework `CharBuilder Setup` rows to report domain values and sources, not report-mechanics language such as `deduplicated`.
- Suppress trivial `status=finished` summaries for helper stages that do not add meaningful metrics.
- Keep source trunk result visible as a compact summary.

## Phase 6: Validation

- Run focused iCTS tests for schema/report and HTree synthesis report behavior.
- Run the user-provided CTS dev command and inspect:
  - total log line count and table count
  - absence of repeated default `HTreeDepth ... Context/Summary` table pairs
  - presence of key result and artifact sections
  - presence of `result/cts/cts_detail.log`
  - presence of detailed HTree/depth/compensation data in `cts_detail.log`
- Run final iCTS quality validation required by project specs:
  `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`

## Risk Areas

- Existing tests may assert old table titles or content.
- Suppressing stage summaries globally would hide failure diagnostics if implemented too broadly.
- Some metrics currently only exist in local finish-field lists; they may need to be moved into result structs before aggregation.
- Dual report sinks may touch setup/config/schema lifecycle code. Keep the first implementation path fixed to `cts_detail.log` beside `cts.log`; avoid broad user-facing config unless needed later.
- Routing useful detail without copying old noise requires judgment. Prefer aggregate detail tables over preserving old repeated stage tables exactly.

## Initial Target Files

- `src/operation/iCTS/source/utils/logger/Schema.hh`
- `src/operation/iCTS/source/utils/logger/Schema.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/solution/SolutionReport.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/characterization/Characterization.cc`
- `src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc`
- `src/operation/iCTS/source/module/characterization/CharBuilderBuild.cc`
