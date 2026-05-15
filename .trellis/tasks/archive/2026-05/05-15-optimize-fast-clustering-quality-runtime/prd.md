# Optimize FastClustering Quality and Runtime

## Goal

Improve iCTS FastClustering so it actually reduces large clock sink sets efficiently while preserving physically compact, legally constrained clusters. The immediate target is the huge `design_clock.def` CTS case where `max_fanout=4` produces low cluster utilization and very high clustering runtime.

## Background / Known Context

- The current huge native run uses `scripts/design/ics55_huge_dev/design_clock.def`, clock net `clock`, and `129494` regular sinks.
- Runtime log shows `Prepare sink loads = 232.807s`, almost entirely inside `FastClustering::run()`.
- The same run produces `44962` cluster buffers, so average fanout is `129494 / 44962 = 2.880`.
- Extracted from `result_clock_native_sdc_full/iCTS_result.def`, cluster sink fanout distribution is:
  - fanout 2: `13412` clusters, `29.83%`
  - fanout 3: `23530` clusters, `52.33%`
  - fanout 4: `8020` clusters, `17.84%`
- With `max_fanout=4`, average fanout should be close to 4 unless real constraints such as diameter or capacitance force more clusters.
- The current clustering config path sets `max_fanout=4`, `max_cap=0.15pF`, and `max_diameter=INT_MAX`; therefore diameter should not force this distribution.
- The development process for this task should not run ecc dev checks unless explicitly requested later.

## Requirements

- Analyze why FastClustering creates too many low-fanout clusters when `max_fanout` is small.
- Distinguish genuine constraint-driven splits from algorithmic or implementation-induced under-utilization.
- Locate the dominant FastClustering runtime bottleneck with evidence.
- Decide whether the runtime issue is primarily algorithmic complexity, implementation inefficiency, or both.
- Improve FastClustering so large designs use fewer clusters when constraints permit, ideally moving average fanout near the configured maximum.
- Improve runtime so clustering is clearly cheaper than downstream H-tree work on large sink sets.
- Preserve correctness for fanout, diameter, capacitance, routing, and assigned-load completeness constraints.
- Keep changes scoped to CTS topology clustering and directly related diagnostics/tests.
- Do not run ecc dev checks during development for this task.

## Acceptance Criteria

- [ ] The task documents the root cause of low average fanout on the 129494-sink huge case.
- [ ] The task documents the root cause of the 232s FastClustering runtime on the huge case with internal timing evidence.
- [ ] The optimized algorithm materially reduces cluster count when `max_fanout=4` and constraints allow packing.
- [ ] The optimized algorithm materially reduces FastClustering runtime on the huge case.
- [ ] The final cluster result remains complete: every valid input load is assigned exactly once.
- [ ] The final clusters remain legal under configured fanout, diameter, and cap checks.
- [ ] Focused iCTS/unit tests or targeted huge-case validation are run; ecc dev checks are explicitly skipped per user instruction.

## Definition of Done

- Build and focused tests pass for touched CTS topology code.
- Huge-case logs include before/after FastClustering runtime and fanout distribution.
- Any remaining QoR risk is summarized with concrete metrics.

## Out of Scope

- Changing analytical H-tree selection or characterization policy.
- Optimizing STA evaluation stalls.
- Running ecc dev checks during development.
- Broad CTS flow refactors outside FastClustering/topology clustering.

## Research References

- `research/initial_fast_clustering_diagnosis.md`
