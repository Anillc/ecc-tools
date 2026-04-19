# Validate arm9 linear_clustering algorithms

## Goal
Finish the arm9 linear-clustering study by productizing the validated strategy set into source code, shrinking the benchmark to representative coverage, and removing temporary or non-competitive configuration paths that only add runtime noise.

## Current Baseline
- Real arm9 and representative synthetic benchmarking infrastructure is already in place.
- Real-tech setup reuses the arm9 placement inputs from `scripts/design/ics55_dev/result/arm9_place.*`, with `.def.gz` and `.v` wired explicitly.
- Process / electrical context is reused from the existing ics55 setup and CTS defaults from `scripts/design/ics55_dev/iEDA_config/cts_default_config.json`.
- The benchmark now covers 100 representative synthetic cases across 5 distribution families and 4 load scales while sharing cached real-tech context.
- A real bug discovered during the study has already been fixed: synthetic exact-cap pin lookup / naming issues no longer crash the experiment path.

## Mainline Workstream
1. Promote the final validated strategy set into production defaults for linear clustering.
2. Keep explicit single-strategy execution available for tests / experiments / manual overrides.
3. Shrink the benchmark and support code to the final retained strategy set.
4. Remove temporary exploration-only logic and clearly non-competitive configurations that waste runtime.
5. Rebuild and rerun targeted validation to confirm the cleaned-up flow still produces arm9 rankings and benchmark artifacts.

## Final Default Strategy Set
Use the following four strategies as the official default exploration set for source-side `linear_clustering`:

1. `discrete_hilbert__classic_index__swap_xy__bits_10__bidirectional_greedy__prefix_and_strided_sweep__strided_count_4`
2. `discrete_hilbert__classic_index_tangent__swap_xy__bits_10__bidirectional_greedy__prefix_and_strided_sweep__strided_count_4`
3. `continuous_hilbert__reverse_greedy__prefix_and_strided_sweep__strided_count_4`
4. `continuous_hilbert__forward_greedy__prefix_and_strided_sweep__strided_count_4`

Rationale:
- The two discrete variants are the best real-arm9 performers from the global arm9 sweep.
- The two continuous variants are the strongest continuous baselines in the representative synthetic benchmark and remain competitive on real arm9.
- This set is intentionally small enough to be usable as a default exploration bundle.

## Requirements
- Source code must expose a clean default exploration path that evaluates the retained four strategies and selects the best legal result.
- Production call sites that currently rely on default linear clustering behavior should use the retained default exploration set rather than a single hard-coded strategy.
- Existing explicit `LinearClusteringConfig` usage must still support single-strategy execution without being silently expanded into multi-strategy exploration.
- Benchmark / experiment code must keep the arm9 real-tech ranking and representative synthetic ranking flow, including `cts.log`, `report.log`, and CSV artifacts.
- Benchmark candidate scope must be reduced from the interim curated-top6 set to the final retained set unless a retained item proves redundant during cleanup.
- Exploration-only benchmark notes, labels, and output-directory names must be updated to reflect the final retained scope.
- Remove clearly non-competitive or unused benchmark-only configurations and temporary code paths that were introduced during broad exploration.
- Keep the cached real-tech / process / electrical setup model so the benchmark remains practical.
- Do not run `ecc_dev_tools` during this task.
- Use subagents to implement the cleanup / productization workstreams.

## Acceptance Criteria
- [ ] `prd.md` reflects the cleanup/productization plan as the task mainline.
- [ ] Source-side linear clustering has an official default exploration helper for the retained strategy set.
- [ ] Default CTS / topology linear-clustering flow uses the retained strategy set instead of a single baked-in strategy.
- [ ] Explicit single-strategy config paths still work for focused tests and experiments.
- [ ] Benchmark coverage remains available and produces ranking artifacts after scope cleanup.
- [ ] Benchmark candidate scope and related reporting are updated to the final retained set.
- [ ] Temporary exploration code and obviously useless config paths are removed or isolated away from the maintained path.
- [ ] Targeted build/test validation passes after cleanup.

## Technical Notes
- Prefer placing reusable default-strategy selection logic under `src/operation/iCTS/source/module/topology/linear_clustering/` rather than duplicating it in flow call sites.
- Keep runtime logging on `LOG_*` and structured report output on the existing schema / artifact helpers.
- Respect the no-exception policy and existing iCTS ownership boundaries.
- Avoid clobbering unrelated user changes in the dirty worktree.
- Validation should use targeted build/test commands only; skip `ecc_dev_tools`.
