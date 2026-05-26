# Mathematical solid analytical H-tree solver design

## Scope

This design describes the migrated production analytical H-tree solver for the mathematical H-tree task. It replaces the removed shortlist/beam prototype work with an optimization-model-centered architecture under the normal H-tree synthesis directory.

## Architecture

The integrated implementation has two boundaries:

- existing characterization data/model collection: exports and validates real characterization samples and fitted functions.
- `synthesis/htree/analytical_solver`: builds, solves, diagnoses, and materializes the mathematical H-tree optimization problem.

The solver boundary must be independent of the concrete optimization backend:

- `MathHtreeProblem`: slots, choices, fitted functions, variable bounds, H-tree multiplicities, legality metadata, and objective settings.
- `MathHtreeSolution`: solver status, objective values, selected choices, continuous boundary values, anchor values, and diagnostics.
- `MathHtreeSolver`: interface for solving a `MathHtreeProblem`.
- `ScipMathHtreeSolver`: external-SCIP MILP implementation that solves generated LP models.
- `MathHtreeMaterializer`: converts selected choices back into existing H-tree segment/topology artifacts and reports exact materialization failures.

## Data Flow

1. Build the minimal legal pattern set from existing characterization/pattern infrastructure.
2. Split the H-tree into a representative ordered slot sequence by wirelength unit or an exactly equivalent aggregation.
3. Attach fitted functions and domains to every slot choice.
4. Build the mathematical optimization model.
5. Solve `min_delay`.
6. Solve `min_power`.
7. Solve the normalized delay/power tradeoff.
8. Materialize the selected integer solution into H-tree structures.
9. Validate legality and record FastSTA prediction diagnostics.
10. Run Evaluation STA/iSTA comparison on real designs.

## Model

For slot `i` and pattern choice `k`:

```text
z_i,k in {0,1}
s_i >= 0
s_eval_i >= 0
s_out_i >= 0
c_i >= 0
c_eval_i >= 0
u_i >= 0
d_i >= 0
p_i >= 0
```

Choice:

```text
sum_k z_i,k = 1
```

The implemented SCIP LP model uses an extended disjunctive formulation instead
of big-M selected equalities.  For every slot-choice pair it creates local
continuous contribution variables:

```text
si_i,k  >= 0
cl_i,k  >= 0
se_i,k  >= 0
ce_i,k  >= 0
```

The slot variables are sums of local contributions:

```text
s_i      = sum_k si_i,k
c_i      = sum_k cl_i,k
s_eval_i = sum_k se_i,k
c_eval_i = sum_k ce_i,k
```

The local variables are activated and bounded by the binary choice:

```text
0 <= si_i,k <= s_max_i,k * z_i,k
0 <= cl_i,k <= c_max_i,k * z_i,k
s_min_i,k * z_i,k <= se_i,k <= s_max_i,k * z_i,k
c_min_i,k * z_i,k <= ce_i,k <= c_max_i,k * z_i,k
se_i,k >= si_i,k
ce_i,k >= cl_i,k
```

The fitted affine relations are then written as one linear equality per metric:

```text
s_out_i = sum_k (a_s_i,k * se_i,k + b_s_i,k * ce_i,k + q_s_i,k * z_i,k)
d_i     = sum_k (a_d_i,k * se_i,k + b_d_i,k * ce_i,k + q_d_i,k * z_i,k)
p_i     = sum_k (a_p_i,k * se_i,k + b_p_i,k * ce_i,k + q_p_i,k * z_i,k)
u_i     = sum_k (a_u_i,k * cl_i,k + q_u_i,k * z_i,k)
```

This is the convex-hull/disaggregated style formulation for a finite union of
affine choice regions. It is larger than the first big-M draft, but gives a
much stronger LP relaxation and fixed the `ics55_dev` anchor timeout/gap issue.
It also keeps `slew` and `cap` continuous; no bins, DP states, beam, top-K, or
frontier truncation are used by the accepted solver path.

Continuity:

```text
s_{i+1} = s_out_i
c_i = downstream_cap_multiplier_i * u_{i+1}
```

Totals:

```text
total_delay = weighted sum of d_i according to H-tree level semantics
total_power = weighted sum of p_i and source-boundary corrections according to H-tree level semantics
```

The accepted implementation is an affine MILP. Quadratic terms remain deferred
until fit evidence requires them and a supported MIQP/MIQCP/MINLP backend is
documented.

## Objective

The default solve sequence is:

```text
D_min = optimum of min total_delay
P_min = optimum of min total_power
optimum = min total_delay / D_min + total_power / P_min
```

If either anchor is infeasible, zero, numerically invalid, or timeout-only without a usable gap, the normalized solve must fail with a clear diagnostic rather than silently choosing another objective.

The implemented status policy is:

- `optimal`: accepted without gap qualification.
- `feasible_with_gap`: accepted only when SCIP wrote a complete variable
  assignment and a finite parsed gap; for anchor solves the current usable-gap
  threshold is 25%.
- `timeout`: not accepted as an anchor unless it was upgraded to
  `feasible_with_gap` by the rule above.
- `abnormal`: used for command failure, missing solution status, or missing
  variable assignments.

`ics55_huge_dev` currently completes with `feasible_with_gap` at 11.31% for the
combined three-solve sequence. `ics55_dev` completes with `optimal`.

## Legality

Legality should be represented in the model whenever practical:

- selected pattern feasibility,
- characterization-domain feasibility,
- max slew,
- max load,
- max fanout,
- branch/root buffering constraints,
- source cap/load continuity,
- materialization compatibility.

Some legality can only be proven after using existing materialization code. In that case, the materialization failure must be turned into a precise model refinement when possible, such as a no-good cut for the complete invalid integer assignment. This refinement loop must not become a hidden search strategy and must report every rejection.

## Runtime Strategy

Runtime improvement must come from exact model reductions:

- represent repeated branches with multiplicity weights,
- aggregate identical slot structures only when variables and constraints remain equivalent,
- use tight bounds from fitted domains and electrical limits,
- remove a pattern only when it is mathematically dominated for all feasible continuous inputs,
- let the solver backend use presolve/cuts/branch-and-bound,
- avoid building per-sink or per-full-sequence models for large designs.

## Solver Backend

The first backend class is affine MILP with SCIP. Direct OR-Tools C++ integration was validated in a standalone probe, but importing the OR-Tools C++ package into the iEDA build conflicts with the repository's existing Abseil targets. The current integration therefore uses the local OR-Tools package's external `bin/scip` executable on generated LP files. The process is launched with `posix_spawn`, not a shell command processor; SCIP writes its own log through `-l <logfile>`.

This keeps solver execution mathematical and status-driven while avoiding dependency contamination in the main build.

The implementation must record:

- SCIP binary and library path,
- backend name,
- support for binary variables,
- generated extended formulation and local contribution variables,
- generated LP model files when retained for debug,
- status mapping into project diagnostics.

If affine MILP is insufficient and quadratic equations are required, implementation stops at a solver-capability report unless a supported MIQP/MIQCP/MINLP backend is confirmed.

## Outputs

The solver reports:

- fit-validation reports,
- structured problem JSON or equivalent model dump,
- LP/MPS dump when supported,
- solution JSON with selected patterns and continuous values,
- materialization report,
- real-design comparison report.

## Compatibility

The new path is opt-in through `enable_analytical_htree`. Native H-tree remains the default path and keeps its frontier-based selection, with the memory compaction fix applied to avoid retaining unreachable topology pattern nodes.

## Risks

- Affine fits may not meet timing/power error requirements.
- Quadratic equality models may be nonconvex and need a solver not currently installed.
- The extended formulation is stronger than big-M but introduces more continuous variables; huge currently spends most end-to-end time outside H-tree, in CTS optimization and final Evaluation.
- A mathematically clean model may still miss materialization constraints from the existing implementation; these must be surfaced as constraints or exact rejection cuts.
