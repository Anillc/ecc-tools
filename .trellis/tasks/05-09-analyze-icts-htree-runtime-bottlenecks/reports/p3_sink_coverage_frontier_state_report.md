# P3 Sink-Load Coverage State in Frontier Composition

Date: 2026-05-09

> Superseded on 2026-05-09 by `rollback_to_opt3_report.md`. This file is a historical analyzer report. The P3 analyzer code and focused tests described below were removed when production H-tree code was restored to `refs/backups/icts-runtime-pre-next-optimizations-20260509-131717`. P3 remains investigation-only and is not default-enabled.

## Scope

P3 evaluated whether iCTS H-tree can push sink-load coverage state into topology frontier composition and discard states whose
`leaf_load_cap_idx` cannot cover the required real leaf-side load cap before the current post-hoc global coverage pass.

Inputs reviewed:

- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/reports/runtime_complexity_and_next_optimizations.md`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/reports/p2_strict_pre_comp_gate_report.md`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/region/SinkLoadRegion.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentLibrary.hh`
- `src/operation/iCTS/source/module/characterization/Frontier.hh`
- `src/operation/iCTS/source/module/characterization/HashJoinEngine.hh`
- `src/operation/iCTS/source/module/characterization/HTreeTraits.hh`

## Decision

P3 is not enabled as default production pruning.

Implemented gated state and instrumentation only:

- `TopologyPatternLibrary` can carry an incremental sink-load boundary state beside existing composition state, but only after
  `enableSinkLoadBoundaryStateTracking()` is called by the debug analyzer path.
- The default topology pattern node does not store P3 boundary state, so normal builds do not pay a per-pattern memory/write cost for the
  analyzer cache.
- `SinkLoadRegion` exposes helpers to resolve:
  - post-hoc signature from materialized topology pattern metadata;
  - tracked incremental signature from the analyzer-only topology pattern boundary-state side cache;
  - legality directly by signature.
- `ICTS_HTREE_DEBUG_SINK_COVERAGE_FRONTIER_STATE=1` runs a non-invasive analyzer. It compares incremental vs post-hoc signatures and
  counts how many raw pre-root-compensation entries would fail required leaf-load cap coverage.
- No production pruning path was added. No env-gated pruning prototype was added for P3.

The reason is correctness risk. The current evidence proves the incremental final-pattern signature can match the post-hoc signature on the
tested case, but it does not prove that discarding entries during hash-join composition preserves the final compensated Pareto-affecting set.

## Feasibility Findings

The bottom-most buffered boundary can be represented incrementally.

For a seed segment:

- buffered segment -> `bottom_most_buffered_level = 0`, `segment_pattern_id = seed segment`;
- wire segment -> no buffered boundary, represented as `bottom_most_buffered_level = -1`.

For concat:

- if downstream has a buffered boundary, it remains the bottom-most buffered boundary after concat, with its level shifted by
  `upstream_levels`;
- otherwise the upstream boundary state is inherited.

This matches the post-hoc materialized pattern signature at full topology depth in the focused unit test and in the `ics55_dev` debug run.
The tracked state is only maintained for analyzer/debug runs; the default path still resolves legality from materialized topology pattern
metadata.

However, the required leaf-load cap is not only a pattern property. It also depends on:

- the actual topology boundary level: `bottom_most_buffered_level + 1`;
- the real load groups at that topology level;
- the last buffer position of the boundary segment;
- exact clustering electrical evaluation and routing-cap lower bound;
- the monotone hard-fail cache in `SinkLoadRegionLegalityContext`.

Therefore a safe compose-time pruning proof would need to track the partial frontier's absolute level offset. The current hash-join state only
sees the local pattern state; during leaf-to-root construction, a partial suffix's local `bottom_most_buffered_level` is not the same number as
the absolute level in the final topology until the final root side has been prepended.

## Experiment

Command:

```bash
cd scripts/design/ics55_dev
ICTS_HTREE_DEBUG_SINK_COVERAGE_FRONTIER_STATE=1 ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Artifacts:

- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p3_sink_coverage_frontier_state_debug/run.log`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p3_sink_coverage_frontier_state_debug/time.txt`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p3_sink_coverage_frontier_state_debug/cts.log`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/p3_sink_coverage_frontier_state_debug/p3_debug_summary.txt`

Debug analyzer result:

| Depth | Signatures match | Checked raw pre-comp entries | Legal entries | Covered entries | Would prune by leaf cap | Signature mismatches |
| ---: | :---: | ---: | ---: | ---: | ---: | ---: |
| 8 | yes | 380607 | 380607 | 319435 | 61172 | 0 |
| 7 | yes | 220756 | 220756 | 148481 | 72275 | 0 |
| 6 | yes | 169883 | 169883 | 90152 | 79731 | 0 |
| 5 | yes | 243981 | 236991 | 124338 | 112653 | 0 |
| total | yes | 1015227 | 1008237 | 682406 | 325831 | 0 |

Interpretation:

- The incremental signature cache matched post-hoc materialization for every checked final raw frontier entry in this run.
- The current fixture has substantial leaf-cap coverage pruning opportunity: `325831 / 1008237` legal raw pre-comp entries failed required
  leaf-load cap coverage.
- Depth 5 had `6990` sink-load-region illegal entries before leaf-cap coverage; the rest of the rejected entries were coverage failures.
- This is promising as an analysis signal, but it is not an equivalence proof for compose-time pruning.

The selected result remained aligned with prior P1/P2 runs:

| Metric | Value |
| --- | --- |
| selected depth | 5 |
| selected topology pattern id | 10297477 |
| selected level segment pattern ids | `522717,468375,28,24,6` |
| selected compensated H-tree metric | `0.4959 ns / 217.271 uW` |
| selected physical root load | `0.1428 pF` |
| H-tree inserted buffers | 40 |
| final clock buffer count | 360 |
| total clock network wirelength | `43151.203 um` |
| setup WNS / hold WNS | `7.302 ns / 0.008 ns` |

Runtime for the debug analyzer run:

| Metric | Value |
| --- | ---: |
| CTS synthesis | 33.263 s |
| CTS total | 49.517 s |
| external wall | 71.16 s |
| max RSS | 6966388 KB |

The debug analyzer adds extra signature and legality work and should not be used as a production runtime measurement.

## Why Default Pruning Is Still Unsafe

There are three separate proof gaps.

1. Compensation-order risk remains.

P2 already established that moving coverage before root-driver compensation can change same-state delay/power dominance unless equivalence is
proven. P3 would prune even earlier, before some state-frontier pruning and before root compensation. That is strictly higher risk than P2.

2. Partial-frontier level offset is not encoded in the hash-join key.

The incremental boundary state stored on a topology pattern is local to that pattern. A partial suffix built from the leaf side needs an
absolute topology level offset before it can call `SinkLoadRegion` against the real tree. Evaluating required cap with only the local level
would query the wrong load groups before final assembly.

3. Pattern identity and downstream dominance can drift.

Hash-join composition assigns sequential topology pattern ids. If a compose-time pruning path skips entries before later combinations, later
pattern id assignment and equal-cost tie-break behavior can drift. Even if the selected scalar delay/power matches on one design, the
Pareto-affecting topology set may differ.

## Tests Added

Focused unit coverage was added in `HTreeTest`:

- `IncrementalSinkLoadRegionSignatureMatchesPostHocSignature`: constructs a three-level topology pattern with buffered source and sink
  segments, explicitly enables tracked sink-load boundary-state maintenance, and verifies the cached incremental signature matches the
  post-hoc materialized signature.
- `IncrementalSinkLoadRegionSignatureMismatchIsDetectable`: explicitly enables tracked state, intentionally stores a wrong incremental state,
  and verifies the mismatch is detectable by comparing incremental and post-hoc signatures.

Existing P1 lazy fallback tests and P2 strict-pre-comp equivalence tests still pass.

## Recommendation

Keep P3 as instrumentation only for now.

The next safe step is an env-gated prototype that does not prune during intermediate joins yet:

1. carry absolute topology level offset in the compose loop;
2. at each level, compute a candidate pre-comp covered frontier by signature using a copied `SinkLoadRegionLegalityContext`;
3. compare against the current post-compensation covered Pareto-affecting set, as P2 does;
4. only after broad equivalence evidence, prototype actual compose-time pruning behind a separate env variable.

Promotion should require a design/config matrix, including forced fallback or no-strict-feasible cases, not just `ics55_dev 0.5/0.5`.
