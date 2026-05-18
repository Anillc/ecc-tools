# Design: CTS optimization runtime bottleneck

## Objective

Make the fast-STA-backed optimization runtime observable at stage granularity, then use `ics55_huge_dev` to prove where the 30-minute stall occurs. The first deliverable is evidence, not a search-quality rewrite.

## Boundaries

`flow/optimization` owns optimization-stage orchestration and reporting. It may emit stage timing around calls into fast STA and candidate search because it already owns the flow boundary.

`database/adapter/fast_sta` owns timing, power, cap, slew, and graph update semantics. If instrumentation needs lower-level detail inside build/update calls, add narrow counters/timers there without changing numerical behavior.

No algorithmic behavior should change in the profiling pass. Any later performance fix must be separated from instrumentation and validated against the same counters.

## Instrumentation Plan

Add compact runtime summaries under the existing schema/log infrastructure.

Optimization-level timers:

- `build_route_tree_cache`
- `build_fast_sta_context`
- `inject_route_trees`
- `collect_optimizable_buffers`
- `collect_cap_baseline`
- `collect_slew_baseline`
- `solve_clock_total`
- `apply_mutations`

Solver-level timers:

- `capture_initial_state`
- `build_topology_index`
- `generate_batch_candidates`
- `trial_apply_update_capture_restore`
- `apply_accepted_batch`
- `capture_final_state`

Fast STA lower-level counters/timers if the first run proves context-build or trial-update is dominant:

- node count, net count, sink count, buffer input/output count;
- full timing update runtime;
- full power update runtime;
- region timing/power update runtime if used;
- buffer input lookup count and fallback scan count;
- batch master change count and changed node count;
- route-tree/parasitic RC node/edge counts.

The expected first question is whether time is spent before or after `solveClock(...)`. Because the huge probe emitted only `CTS Optimization Setup`, the first instrumentation should be visible immediately after each per-clock stage so the next huge run can stop once a dominant stage is known.

## Likely Bottleneck Shape

The code currently has repeated full-node scans for buffer input/output pairing:

- `FastStaTiming::propagateBufferOutput(...)` calls `findBufferInputNode(...)`, which scans all `context.nodes`.
- `FastStaPower::calcBufferPower(...)` calls `findBufferInputNode(...)`, which scans all `context.nodes`.
- `FastStaIncremental::normalizeBufferInputNodeId(...)` also scans all `context.nodes`.

For the huge probe, the topology had over 122k sinks and over 14k inserted buffers. A buffer-count times node-count scan can be billions of comparisons before any candidate search starts. The task should prove or disprove this with timers/counters before changing lookup structures.

If proven, the likely low-risk fix is to store or build a buffer input/output index once per fast STA context and use O(1) lookup in timing, power, and incremental updates. That fix must preserve existing node ids and mutation behavior.

## Validation Strategy

Use a short validation ladder:

1. Build affected targets:

```bash
ninja -C build icts_source_database_adapter_fast_sta icts_source_flow_optimization
```

2. Run `ics55_dev` at 80ps to ensure instrumentation does not break normal small-case behavior.

3. Run `ics55_huge_dev` 80ps with the existing huge config and separate result dir.

4. Stop the huge run after the bottleneck has been measured if it still does not complete in a practical time.

5. Only after a performance fix, rerun:

- `ics55_dev` 80ps/40ps/0ps for QoR regression;
- `ics55_huge_dev` 80ps for runtime progress;
- 40ps/0ps huge only if 80ps reaches the optimization summary within a practical runtime.

6. Run final full iCTS check after source changes converge:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Reporting

Default logs should report compact per-clock metrics:

- stage runtime;
- graph sizes;
- candidate and trial counts;
- accepted mutation count;
- cap/slew rejection counts;
- memory sample if available through existing runtime metric support.

Do not add per-sink, per-path, or per-trial verbose logs by default.
