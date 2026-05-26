# Mathematical solid analytical H-tree solver

## Goal

Rebuild analytical H-tree construction as a mathematically formulated optimization solver. The solver must choose buffering patterns for an H-tree slot sequence with binary/integer pattern-choice variables and continuous electrical variables, constrained by fitted characterization functions and explicit legality constraints.

The accepted production path must be defined by a mathematical optimization model and a solver status. It must not depend on fixed cap/slew bins, value-state DP, beam search, top-K candidate lists, arbitrary frontier truncation, or large full-sequence enumeration.

## User Value

The current analytical H-tree flow is useful for scale, but its solution quality is still tied to candidate generation and pruning. The requested replacement should make the solution space, feasibility conditions, objective, and solver result explicit. This gives a more defensible algorithm for large designs and makes runtime/QoR/error tradeoffs easier to inspect.

The final validation target is `ics55_huge_dev`. QoR does not need to comprehensively beat native H-tree, but the produced result must be legal, timing error must be reasonable, and runtime must show a significant improvement over the current path.

## Current Cleanup State

- The previous active fixed-bin/value-state task has been removed from `.trellis/tasks`.
- The previous archived value-bound task has been removed from `.trellis/tasks/archive/2026-05`.
- The previous experimental implementation directories have been removed before creating new implementation:
  - `src/operation/iCTS/source/module/analytical_characterization_exp`
  - `src/operation/iCTS/source/flow/synthesis/htree/analytical_solver_exp`
  - `src/operation/iCTS/test/flow/synthesis/htree/analytical_solver_exp`
- The new task directory is the only active task for this work:
  - `.trellis/tasks/05-25-math-solid-analytical-htree-solver`

The same directory names may be recreated later, but only for the new mathematical formulation. No previous prototype code, naming, or implementation structure may be carried forward as the accepted solver.

## Known Facts

- Existing characterization samples `slew_in` and `cap_load`, queries FastSTA, and fits analytical models for metrics such as delay, output slew, power, source boundary power, and source-cap propagation.
- Prior analysis indicates many characterization query values can be approximated well as affine or quadratic functions of `slew_in` and `cap_load`.
- The existing H-tree analytical flow keeps a limited candidate set/frontier to support large designs. This task replaces that mechanism for the new accepted solver.
- The local OR-Tools C++ package is available at:

```text
~/download/or-tools_x86_64_Ubuntu-20.04_cpp_v9.14.6206/
```

- Python validation, when needed, must run under:

```bash
conda activate nu_htree
```

- Final real-design validation commands:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl

cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_huge_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- Intermediate development must not run the ECC dev check. Run it only after final implementation is complete:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Required Solver Semantics

The accepted solver must be an optimization model with:

- binary/integer variables for pattern choices,
- continuous variables for input slew, output slew, downstream load cap, upstream/source cap, delay, and power,
- explicit fitted-function constraints for selected patterns,
- explicit electrical continuity constraints between adjacent slots,
- explicit H-tree level/branch multiplicity in delay and power totals,
- explicit domain bounds from characterization data,
- explicit legality constraints where they can be represented mathematically,
- deterministic objective construction,
- solver status reporting including optimal, feasible with gap, infeasible, unbounded, timeout, and numerical failure when supported by the backend.

The production answer must come from the solver's selected variables. Post-solve validation may reject a solution and add exact model refinements such as no-good cuts or missing legality constraints, but validation cannot become a hidden beam, top-K, shortlist, or enumeration mechanism.

## Mathematical Model Requirements

The model starts from a slot sequence. Each slot represents a wirelength-unit piece or an equivalent exact aggregation of repeated pieces. Each slot has a set of legal buffering patterns and fitted metric functions.

For slot `i` and candidate pattern `k`, the model must include:

- `z_i,k`: binary variable selecting whether pattern `k` is used at slot `i`,
- `s_i`: continuous input slew at slot `i`,
- `s_out_i`: continuous output slew of slot `i`,
- `c_i`: continuous downstream load cap seen by slot `i`,
- `u_i`: continuous upstream/source cap presented by slot `i`,
- `d_i`: continuous delay contribution,
- `p_i`: continuous power contribution.

Required constraints:

- exactly one pattern selected per slot,
- root slew boundary,
- leaf/load boundary,
- cap continuity from downstream source cap to upstream load cap,
- slew continuity from upstream output slew to downstream input slew,
- selected fitted-function relations for output slew, delay, power, and source cap,
- selected function-domain bounds for `slew_in` and `cap_load`,
- max slew, max load, fanout, root-buffer/root-compensation, and materialization legality constraints when expressible,
- exact H-tree branch multiplicity for totals.

Preferred fitted-function class:

- Affine functions are the first production target because they produce MILP constraints.
- Quadratic functions may be used only after documenting the resulting optimization class and confirming solver support.
- Nonlinear/global optimization is a last resort and requires an explicit solver dependency report before implementation.

## Objective Requirements

The default objective must be dimensionless and deterministic:

1. Solve or otherwise mathematically derive a legal `min_delay` anchor.
2. Solve or otherwise mathematically derive a legal `min_power` anchor.
3. Solve the normalized tradeoff:

```text
minimize total_delay / min_delay + total_power / min_power
```

Weights may be added later, but the default validation path must report the unweighted normalized objective and the two anchors.

## Hard Exclusions

The accepted solver path must not use:

- fixed cap/slew bins as state,
- cap/slew lattices as state,
- value-state DP,
- artificial boundary buckets,
- beam search,
- top-K candidate selection,
- arbitrary shortlist pruning,
- heuristic frontier truncation,
- large full-pattern-sequence enumeration,
- EDA/CTS literature as the design basis for this task.

Sampling grids are allowed only for characterization data collection and fit validation. They must not become optimization state.

Exhaustive enumeration is allowed only as an offline oracle for very small validation instances. It must not appear in the production solver path.

## Allowed Runtime Strategies

Runtime improvements must preserve the mathematical model:

- exact H-tree symmetry aggregation,
- multiplicity weights for repeated levels or branches,
- solver presolve,
- tight variable bounds,
- valid inequalities,
- exact dominance removal proven over the full continuous domain,
- exact decomposition with explicit linking constraints,
- warm starts if they do not restrict the feasible region,
- no-good cuts for exact rejection of already-invalid complete integer assignments.

Any heuristic fallback must be labeled as debug-only and cannot satisfy acceptance.

## Required Research

Research must focus on mathematical optimization methods and solver capability, not EDA/CTS literature. The task must record accepted, deferred, and rejected formulation choices, including:

- affine MILP with indicator constraints or tight big-M,
- disjunctive programming formulation,
- McCormick or perspective reformulations if binary-continuous products are introduced,
- convex MIQP/MIQCP feasibility if quadratic functions are necessary,
- MINLP/global nonlinear requirements only if the above are insufficient,
- OR-Tools backend capability and limitations for the chosen formulation.

## Implementation Scope

The mathematical solver is integrated directly under the normal H-tree solver
path:

- `src/operation/iCTS/source/flow/synthesis/htree/analytical_solver`

The old analytical shortlist/beam path is removed from the active build. Native
H-tree remains the default behavior; the mathematical path is enabled only by
the CTS config key `enable_analytical_htree`. Native H-tree also receives the
reachable-pattern memory compaction fix required for huge designs.

## Validation Requirements

Fit validation must use real characterization data and report at least:

- sample count,
- pattern/slot identity,
- basis type,
- coefficients,
- R2,
- RMSE,
- max absolute residual,
- max relative residual,
- domain coverage,
- monotonicity or physical sanity checks where applicable.

Solver validation must report:

- model size,
- variable and constraint counts,
- backend solver name,
- solver status,
- objective value,
- optimality gap when available,
- `min_delay` and `min_power` anchors,
- selected pattern sequence,
- continuous boundary values,
- materialization/legality result.

Real-design validation must compare against native H-tree on:

- CTS total runtime,
- downstream H-tree runtime,
- peak RSS,
- H-tree buffer count,
- final buffer count,
- wirelength,
- setup/hold WNS or existing QoR metrics,
- FastSTA prediction versus Evaluation STA/iSTA error.

## Acceptance Criteria

- [x] Old active/archive fixed-bin/value-state Trellis task directories are removed from the active worktree.
- [x] Old experimental implementation directories are removed before new implementation.
- [x] This task contains fresh `prd.md`, `design.md`, `implement.md`, and `formulation-options.md` documents for the new mathematical solver.
- [x] The accepted formulation is documented with variables, domains, constraints, objective, solver class, solver status interpretation, and fallback policy.
- [x] The formulation-options document records accepted, deferred, and rejected mathematical choices without preserving previous prototype routes as alternatives.
- [x] Real characterization export and fit validation demonstrate whether affine models are sufficient.
- [x] If affine models are insufficient, the quadratic/MINLP solver requirement is explicitly justified before implementation.
- [x] The accepted solver path is implemented without fixed bins/lattices, value-state DP, beam/top-K, heuristic frontier truncation, or large sequence enumeration.
- [x] The integrated solver produces legal H-tree results on `ics55_dev`.
- [x] The integrated solver completes on `ics55_huge_dev` without the previous high-memory/OOM behavior.
- [x] Final comparison reports runtime, QoR, memory, and Evaluation STA/iSTA error against native H-tree.
- [x] Final code passes the required iCTS ECC dev check after implementation is complete.

## Out of Scope

- Reworking FastSTA or iSTA internals beyond what is needed for data export and error comparison.
- Replacing the complete CTS flow outside the H-tree synthesis integration points.
- Maintaining the removed fixed-bin/value-state prototypes.
- Using heuristic candidate-generation paths to satisfy the new solver acceptance.

## Validation Summary

`ics55_dev` final comparison:

- Native result: `scripts/design/ics55_dev/result_current_migrated_native_fix_20260526_1447`.
- Analytical result: `scripts/design/ics55_dev/result_current_migrated_math_fix_20260526_1447`.
- Native H-tree/CTS/wall/RSS: 2.150 s / 14.322 s / 0:35.34 / 5,272,492 KB.
- Analytical H-tree/CTS/wall/RSS: 9.324 s / 21.221 s / 0:42.16 / 5,571,356 KB.
- Analytical solver status: `optimal`, gap 0, 1026 variables, 4169 constraints.
- Final Evaluation STA/iSTA: native setup/hold WNS 7.317 ns / 0.044 ns;
  analytical setup/hold WNS 7.324 ns / 0.045 ns.
- Both runs are cap/slew legal and meet the 0.080 ns skew target.

`ics55_huge_dev` final comparison:

- Native result: `scripts/design/ics55_huge_dev/result_current_migrated_native_fix_20260526_1343`.
- Analytical result: `scripts/design/ics55_huge_dev/result_current_migrated_math_fix_20260526_1354`.
- Native H-tree/CTS/wall/RSS: 232.789 s / 534.871 s / 9:42.18 / 73,905,232 KB.
- Analytical H-tree/CTS/wall/RSS: 59.939 s / 574.141 s / 10:19.80 / 21,749,964 KB.
- Analytical solver status: `feasible_with_gap`, gap 11.27%, 2184 variables,
  15032 constraints, solver wall 57.788 s.
- Analytical solver totals: delay 0.489159 ns, power 0.040147 W; validated
  totals: delay 0.610728 ns, power 0.040159 W.
- Final Evaluation STA/iSTA: native setup/hold WNS 3.492 ns / 0.071 ns;
  analytical setup/hold WNS 3.487 ns / 0.076 ns.
- Both runs are cap/slew legal. Both miss the 0.080 ns skew target; analytical
  has better optimized skew, but slower downstream optimization.
- Analytical reduces huge H-tree runtime by about 3.88x and peak RSS by about
  70.6%. End-to-end runtime is slower because downstream optimization takes
  436.505 s for analytical versus 227.067 s for native; this is retained as a
  follow-up objective-modeling issue.

## Resolved Questions

- Affine MILP is sufficient for the current production validation path because
  both `ics55_dev` and `ics55_huge_dev` complete with legal final Evaluation STA.
- OR-Tools direct C++ integration remains deferred due to Abseil export conflicts;
  the accepted backend uses the external SCIP executable shipped with the local
  OR-Tools package.
- Root compensation remains represented through the existing H-tree/root-driver
  integration after the mathematical downstream H-tree selection; native
  behavior remains opt-in unchanged.

## Open Questions

- Whether future non-huge designs expose affine residual classes that require a
  quadratic or robust MILP/MIQP extension.
- Whether replacing external SCIP LP execution with an in-process solver API is
  worth the dependency work after the Abseil conflict is resolved. The current
  external execution path uses `posix_spawn` plus SCIP `-l` logging, not
  `std::system`.
- Whether root compensation should eventually be moved directly into the MILP as
  an explicit root slot, instead of using the current existing integration point.
