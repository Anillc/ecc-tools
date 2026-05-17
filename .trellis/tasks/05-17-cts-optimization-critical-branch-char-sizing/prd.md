# CTS optimization critical-branch char sizing

## Goal

Implement a CTS optimization stage that runs after CTS synthesis and before instantiation. The stage sizes existing clock-tree buffers to reduce sink-arrival skew using only H-tree basic characterization through `CharTimingLookup`.

The optimizer must follow a critical-pair guided loop:

1. Evaluate all sink arrival times for the current clock tree assignment.
2. Find the min-arrival sink and max-arrival sink.
3. Find their LCA in the rooted clock tree.
4. Walk from the LCA toward the max-arrival sink in root-to-sink order.
5. Try current-or-larger buffer sizing on that branch, accepting a change only when full-tree skew improves and no new cap violation is introduced.
6. Repeat until target skew is met, no improvement remains, or a bounded iteration/candidate budget is reached.

## Requirements

- Optimization is a peer flow stage, not part of synthesis. It runs after successful `Synthesis::run(...)` and `DESIGN_INST.rebuildClockDAG()`, before `Instantiation::run()`.
- The flow directory name is `src/operation/iCTS/source/flow/optimization`.
- The algorithm module directory is `src/operation/iCTS/source/module/buffer_sizing`.
- Generic rooted-tree LCA support belongs under `src/operation/iCTS/source/utils/graph`.
- Use the name `CharTimingLookup` for the char-backed timing lookup layer.
- Remove/avoid old LCB window sizing, old greedy/tree area-first bounded search, delay fitting, and analytical/fitted timing models from this task's implementation.
- Timing in the search loop must use only H-tree basic char data via `CharTimingLookup`.
- Do not run full STA inside optimization.
- Do not introduce hand-written delay/slew formulas.
- Use bilinear interpolation over char slew/cap lattice data. Boundary clamping is allowed for char lookup coverage, but it must not relax cap legality.
- Candidate sizing is current-or-larger only. Downsizing, insertion, deletion, rerouting, and topology restructuring are out of scope.
- Candidate order on a critical branch is root-to-sink, starting immediately below the LCA toward the current max sink.
- Max sink identity may change after an accepted mutation. The only skew criterion is that global full-tree skew does not worsen and accepted mutations must strictly improve skew beyond epsilon.
- Cap legality uses config max cap and the no-new-violation rule:

```text
if baseline_load_cap <= max_cap:
  selected_load_cap <= max_cap
else:
  selected_load_cap <= baseline_load_cap
```

- Cost reporting uses area only. Area is not the primary objective; it is a tie-breaker among improving candidates.
- Target skew is `CONFIG_INST.get_skew_bound()`.
- If target skew is reachable, stop when target is met.
- If target skew is not reachable, apply the best monotonic improvements found by the greedy loop; do not keep improvements report-only merely because the target remains unmet.
- Logs must be concise by default and include:
  - runtime,
  - target skew,
  - before skew,
  - optimized skew,
  - improvement,
  - iteration count,
  - accepted mutation count,
  - rejected candidate count,
  - cap-rejected count,
  - total area delta,
  - master transition distribution.
- Default logs must not emit large decision tables, fit tables, or full path dumps.
- The development loop must not run `ecc_dev_tools`; run the full iCTS ecc dev check only after implementation and targeted validation converge.

## Acceptance Criteria

- [ ] Unpushed old optimization commits and dirty changes are removed from the active branch, with a backup ref retained.
- [ ] Old optimization task directories that conflict with this task are removed from active tasks.
- [ ] `Flow` owns a shared `CharacterizationLibrary` and passes it to synthesis and optimization.
- [ ] `Synthesis::run(...)` accepts the shared char library.
- [ ] `Optimization::run(...)` executes after synthesis DAG rebuild and before instantiation.
- [ ] `utils/graph` provides tested rooted-tree LCA and ancestor-path functionality.
- [ ] `module/buffer_sizing` provides `CharTimingLookup` and the critical-branch sizing solver.
- [ ] The solver uses explicit problem data and does not read design/config/STA singletons.
- [ ] Flow-side problem construction reads design/config/STAAdapter data and builds explicit module inputs.
- [ ] Timing lookup uses bilinear interpolation from char data, not fitting.
- [ ] Unit tests cover LCA queries, char bilinear interpolation, root-to-sink candidate ordering, cap rejection, accepting an improvement when max sink changes, and rejecting non-improving candidates.
- [ ] Flow tests cover optimization stage no-op/optimized reporting behavior.
- [ ] Targeted iCTS tests and real design smoke pass.
- [ ] Final full `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` is run after implementation convergence and findings are fixed or reported.
- [ ] No commit is made before user review.

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
