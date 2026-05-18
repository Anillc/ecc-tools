# CTS optimization runtime bottleneck

## Goal

Locate the runtime and memory bottleneck that prevents fast-STA-backed CTS optimization from completing on ics55_huge_dev, then define the minimum instrumentation and evidence needed before algorithm changes.

## Background

The current fast-STA-backed CTS optimization task works on `ics55_dev`, but a probe on `ics55_huge_dev` did not reach the first optimization clock summary within 30 minutes:

- Binary: copied from `scripts/design/ics55_dev/iEDA` to `scripts/design/ics55_huge_dev/iEDA`, hash-matched.
- Case: `clock` / `n194404`, target skew `0.08 ns`.
- Design size from log:
  - valid sinks: `122010`;
  - clustered local loads: `30510`;
  - downstream H-tree inserted insts/nets: `14224 / 14224`.
- Synthesis completed:
  - `CTSFlow` synthesis elapsed time: `144.638 s`.
- Optimization reached setup:
  - `timing_source=cts_fast_sta_incremental`;
  - `target_skew=0.0800 ns`;
  - `candidate_master_count=4`.
- No `CTS Optimization Clock Summary` was emitted before interruption.
- Process stayed CPU-active at about one core with roughly `44.3 GB` RSS.
- The run was interrupted after `/usr/bin/time` reported `real 1820.45`.

The saved evidence is:

- `scripts/design/ics55_huge_dev/.trellis_huge_opt_80ps.log`
- `scripts/design/ics55_huge_dev/result_trellis_huge_opt_80ps/cts/cts.log`
- `.trellis/tasks/05-17-cts-optimization-critical-frontier-batch-sizing/results.md`

## Confirmed Code Facts

- `Optimization::run()` emits `CTS Optimization Setup` before entering the per-clock solver loop.
- After setup, the next expensive boundary is:
  - `FastStaAdapter::buildClockContext(*clock, clock_layout, clock_index)`;
  - `injectRouteTrees(...)`;
  - `collectOptimizableBuffers(...)`;
  - `collectCapBaseline(...)`;
  - `collectSlewBaseline(...)`;
  - `solveClock(...)`.
- `FastStaAdapter::buildClockContext(...)` currently calls full `updateTiming()` and `updatePower()` before returning.
- `FastStaTiming::propagateBufferOutput(...)` scans all `context.nodes` through `findBufferInputNode(...)` for each buffer output.
- `FastStaPower::calcBufferPower(...)` also scans all `context.nodes` through `findBufferInputNode(...)` for each buffer output.
- `FastStaIncremental::normalizeBufferInputNodeId(...)` scans all `context.nodes`, and the batch trial path may call it repeatedly during candidate validation.
- These scan patterns are plausible huge-case bottlenecks, but the task must prove the dominant runtime section with instrumentation before optimization changes.

## Requirements

- Treat this task as a bottleneck localization task first, not an algorithm rewrite task.
- Preserve current optimization behavior while instrumenting; do not change accepted mutation semantics during profiling.
- Add concise phase timing / counters around the optimization pipeline so the huge-case stall can be assigned to a specific stage.
- Measure at least the following stages:
  - route-tree cache build;
  - fast STA clock context build;
  - route-tree injection;
  - optimizable buffer collection;
  - cap baseline collection;
  - slew baseline collection;
  - topology index build;
  - candidate generation;
  - batch trial application/evaluation/restore;
  - final mutation application, if reached.
- Add node/net/buffer/sink/trial counters needed to interpret runtime.
- Keep default logs compact enough for normal `ics55_dev` use; detailed per-trial logs must not be emitted by default.
- Use `ics55_huge_dev` as the primary reproduction case and stop early if a single stage is proven to dominate.
- Preserve no-new-cap and no-new-slew violation checks in any later performance changes.
- Do not introduce full iSTA into the optimization loop.
- Do not change CTS topology or add/delete buffers.
- Do not run `ecc_dev_tools` during profiling iterations; run the full final check only after source changes converge.

## Initial Hypotheses

1. The run is blocked before `solveClock(...)`, most likely inside `FastStaAdapter::buildClockContext(...)` because no per-clock summary or candidate counters were emitted.
2. The dominant CPU cost may be O(buffer_count * node_count) scans in fast STA timing and power update:
   - timing: buffer output to input lookup;
   - power: buffer output to input lookup.
3. If the run reaches batch trials, the next likely bottleneck is full timing/power recomputation for every batch via `FastStaAdapter::changeBufferMasters(...)`.
4. Memory pressure may come from full fast STA graph snapshots, parasitic RC storage, or external iDB/STA state already loaded before optimization; RSS must be sampled by stage before assigning ownership.

## Out Of Scope

- Replacing the optimization algorithm before the bottleneck is measured.
- Reintroducing char-backed optimization.
- Adding full iSTA or OpenSTA-based candidate evaluation inside the loop.
- Tuning QoR on `ics55_huge_dev` before the optimization runtime is made observable.
- Fixing unrelated synthesis runtime or final report runtime unless instrumentation proves it is part of the observed stall.

## Acceptance Criteria

- [ ] New task artifacts identify the huge-case reproduction command and saved logs.
- [ ] Optimization instrumentation reports per-stage runtime and key graph/search counters.
- [ ] The 80ps `ics55_huge_dev` run reaches either:
  - a clear stage-level bottleneck report before early stop, or
  - normal optimization summary if runtime becomes acceptable.
- [ ] The bottleneck is classified with evidence as one of:
  - fast STA context build;
  - route-tree injection / parasitic build;
  - baseline legality collection;
  - topology/candidate generation;
  - batch trial evaluation;
  - mutation application;
  - another measured stage.
- [ ] If a low-risk mechanical fix is obvious after measurement, it is implemented separately and validated against `ics55_dev` plus the relevant huge stage.
- [ ] Final report summarizes runtime/QoR impact for `ics55_dev` and huge-stage runtime impact for `ics55_huge_dev`.
- [ ] Final full `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` is run after source changes converge.
- [ ] No `.trellis/spec` update is made unless a reusable iCTS profiling/logging convention is established.

## Notes

- Parent task: `.trellis/tasks/05-17-cts-optimization-critical-frontier-batch-sizing`.
- The parent task contains the functional optimization implementation and small-case QoR matrix; this task owns huge-case scalability localization.
