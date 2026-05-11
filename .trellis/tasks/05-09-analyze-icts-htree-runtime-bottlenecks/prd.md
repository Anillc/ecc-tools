# analyze iCTS h-tree runtime bottlenecks and pruning options

## Goal

Design and run a targeted runtime investigation for iCTS H-tree synthesis, using temporary instrumentation where needed, then identify the dominant runtime bottlenecks and recommend algorithmic pruning improvements that preserve the solution space or only lightly affect it.

## What I already know

- The previous max slew experiment showed a large runtime jump when `max_buf_tran` and `max_sink_tran` changed from `0.05 ns` to `0.5 ns`.
- Baseline `0.05/0.05`:
  - `segment_chars = 3960`
  - `final_frontier_count = 9855`
  - `feasible_solutions = 5256`
  - synthesis runtime `15.085 s`
  - total runtime `31.602 s`
- `0.5/0.5`:
  - `segment_chars = 19515`
  - `final_frontier_count = 243981`
  - `candidate_frontier_entry_count = 236991`
  - `feasible_solutions = 130294`
  - synthesis runtime `95.977 s`
  - total runtime `112.511 s`
- The runtime growth strongly correlates with expanded char/frontier search space after output slew overflow disappears.
- Existing artifacts from the previous experiment are under `.trellis/tasks/05-09-compare-icts-max-slew-results/artifacts/`.
- Relevant code is expected under `src/operation/iCTS/source/flow/synthesis/htree/` and characterization data types under `src/operation/iCTS/source/database/characterization/`.

## Assumptions

- Temporary instrumentation may be added and reverted within this task.
- The final deliverable should be an analysis/recommendation report; no permanent optimization patch is required unless a very low-risk reporting-only patch naturally falls out.
- Runtime bottleneck analysis should focus on algorithmic costs in char/frontier composition, root-driver compensation, boundary filtering, and sink-load-region legality, not generic compiler or micro-optimization topics.
- Optimization candidates should be evaluated by expected effect on solution space and QoR risk.

## Requirements

- Create a reproducible experiment plan for the `0.5/0.5` case where runtime is large enough to expose bottlenecks.
- Add temporary instrumentation to collect stage/substage timing and counters around H-tree synthesis internals.
- Run the instrumented binary on the existing `ics55_dev` script.
- Preserve the original run artifacts and clearly separate new experiment artifacts.
- Analyze bottlenecks using measured data plus code-structure reasoning.
- Propose algorithmic pruning options, including feasibility, expected runtime impact, and solution-space/QoR risk.
- Recommend one primary optimization direction with an implementation outline and validation plan.

## Acceptance Criteria

- [ ] A task-local report explains experiment setup, inserted probes, measured bottlenecks, and interpretation.
- [ ] Report includes file/function-level hotspot attribution where possible.
- [ ] Report includes at least three pruning/algorithmic optimization candidates and compares them by feasibility and risk.
- [ ] Recommended option preserves or nearly preserves the solution space, with clear proof/argument for why QoR impact should be limited.
- [ ] Temporary instrumentation state is explicit: either reverted or clearly listed as intentionally left for review.
- [ ] Worktree status is reported.

## Out of Scope

- Full production implementation of the recommended optimizer.
- Broad refactoring outside iCTS H-tree synthesis.
- Pure performance work that does not change algorithmic search cost, such as only changing compiler flags, replacing containers without search-space reasoning, or micro-tuning log output.
- Changes to external iSTA/iDB modules unless absolutely necessary for instrumentation.

## Technical Notes

- Applicable specs:
  - `.trellis/spec/project-constraints.md`
  - `.trellis/spec/backend/directory-structure.md`
  - `.trellis/spec/backend/quality-guidelines.md`
  - `.trellis/spec/backend/logging-guidelines.md`
  - `.trellis/spec/backend/database-guidelines.md`
  - `.trellis/spec/backend/error-handling.md`
  - `.trellis/spec/guides/cross-layer-thinking-guide.md`
  - `.trellis/spec/guides/code-reuse-thinking-guide.md`
- Likely code areas:
  - `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
  - `src/operation/iCTS/source/flow/synthesis/htree/plan/`
  - `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/`
  - `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/`
  - `src/operation/iCTS/source/flow/synthesis/htree/region/`
  - `src/operation/iCTS/source/flow/synthesis/htree/compensation/`
  - `src/operation/iCTS/source/database/characterization/HTreeTopologyChar.hh`
- Existing run command:
  - `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
