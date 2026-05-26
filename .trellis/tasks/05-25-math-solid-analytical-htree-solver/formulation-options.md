# Mathematical formulation options

This file records formulation choices for the clean-slate mathematical H-tree solver. It intentionally does not preserve previous prototype routes as options. Any formulation accepted here must keep slew and cap as continuous optimization variables, not artificial state buckets.

## Accepted First Attempt: Affine MILP

Status: accepted and implemented as the current mathematical solver path.

Model:

- Binary variables select one buffering pattern per slot.
- Continuous variables represent input slew, output slew, downstream load cap, upstream/source cap, delay, and power.
- Selected pattern functions are affine in continuous variables.
- Electrical continuity and legality are encoded as linear constraints.
- The objective is normalized delay/power after solving delay and power anchors.
- The LP writer uses disaggregated local continuous variables per slot-choice
  pair, so selected affine equations are represented as linear convex-hull style
  equalities instead of weak global big-M equalities.

Why it matches the requirement:

- The feasible region is continuous over fitted-function domains.
- Pattern choice is handled by integer variables in a solver, not by heuristic candidate pruning.
- MILP gives a formal status, objective value, feasibility information, and optimality gap where available.
- Runtime can be improved through exact aggregation, tight bounds, and solver presolve without shrinking the feasible region heuristically.

Required checks before production use:

- affine fit residuals are acceptable on exported real characterization data,
- local SCIP backend solves extracted instances with acceptable status and memory.

Implementation note:

- A standalone OR-Tools C++ probe with `find_package(ortools)` and `MPSolver::CreateSolver("SCIP")` compiled and solved a small MIP outside the iEDA build.
- Directly importing the OR-Tools C++ CMake package inside the iEDA build conflicts with the repository's existing Abseil targets because the local OR-Tools package carries a newer Abseil export set.
- The current integration therefore uses the local OR-Tools package's external `bin/scip` executable on generated LP files. The executable is launched with `posix_spawn` and SCIP's `-l <logfile>` option, avoiding shell command processing while keeping the mathematical MILP solver isolated from iEDA's Abseil/Protobuf dependency graph and preserving a solver-defined MILP status.
- The C++ implementation validates this path on a small continuous slot-choice
  MILP, on `ics55_dev`, and on `ics55_huge_dev`.
- `ics55_dev` solve status is `optimal` for the normalized path.
- `ics55_huge_dev` solve status is `feasible_with_gap`; the selected solution
  has a complete SCIP variable assignment, finite parsed gap, and passes
  materialization plus final Evaluation STA.

## Accepted Technique: Disjunctive Extended Formulation

Status: implemented inside the affine MILP.

The first big-M draft solved small instances but produced weak LP relaxations
and poor anchor status on `ics55_dev`. The current implementation follows the
standard disjunctive/convex-hull idea for a finite union of affine regions by
disaggregating the continuous electrical variables:

```text
s_i      = sum_k si_i,k
c_i      = sum_k cl_i,k
s_eval_i = sum_k se_i,k
c_eval_i = sum_k ce_i,k
0 <= si_i,k <= s_max_i,k z_i,k
0 <= cl_i,k <= c_max_i,k z_i,k
s_min_i,k z_i,k <= se_i,k <= s_max_i,k z_i,k
c_min_i,k z_i,k <= ce_i,k <= c_max_i,k z_i,k
metric_i = sum_k affine_i,k(se_i,k, ce_i,k, z_i,k)
source_cap_i = sum_k affine_i,k(si_i,k, cl_i,k, z_i,k)
```

This preserves continuous `slew` and `cap` variables, avoids optimization bins,
and materially improves the solver relaxation.

Decision rule:

- Prefer solver-native indicators when supported.
- The current external LP path uses disaggregation because it is stronger than
  big-M while still expressible in portable LP form.
- Native indicator constraints remain optional future cleanup only if they can
  be integrated without weakening the formulation or disturbing the build
  dependency graph.

## Deferred: Convex MIQP or MIQCP

Status: deferred until affine fit validation proves it is necessary.

Use this only if quadratic fitted functions materially improve required accuracy and the resulting constraints/objective are convex and solver-supported.

Risks:

- Quadratic equality constraints are often nonconvex.
- OR-Tools support may not cover the needed model class.
- Runtime on `ics55_huge_dev` may become unacceptable.

Decision rule:

- Do not implement as production until the quadratic model class, backend, status semantics, and runtime expectation are documented.

## Last Resort: MINLP or Global Nonlinear Optimization

Status: not a default implementation path.

Use this only if affine and convex quadratic formulations cannot satisfy fit accuracy and legality requirements.

Decision rule:

- Stop and report the required solver dependency before implementation.
- Do not approximate the nonlinear model with hidden bins or buckets to force it into the current environment.

## Research Alternative: Exact Network or Extended Formulation

Status: research only.

An exact network, flow, or extended formulation is allowed only if it preserves continuous electrical coupling exactly through variables and constraints.

Decision rule:

- Accept only when it is mathematically equivalent to the continuous constrained slot-choice model.
- Reject if it discretizes cap/slew, drops boundary coupling, or relies on finite candidate shortlists.

## Explicitly Rejected

- Fixed cap/slew bins as optimization state.
- Cap/slew lattices as optimization state.
- Artificial value states or boundary buckets.
- Dynamic programming over discretized electrical states.
- Beam search.
- Top-K pattern/candidate selection.
- Arbitrary shortlist or frontier truncation.
- Full pattern-sequence enumeration for large designs.
- Post-solve validation loops that effectively enumerate alternatives without adding exact model constraints.

## Validation Snapshot

`ics55_dev` with the mathematical solver:

- H-tree selected depth: 11.
- Solver status: `optimal`.
- MILP size: 1026 variables, 156 binaries, 870 continuous variables, 4169 constraints.
- Solver time: 9.212 s.
- Solver objective/gap: 2.091445 / 0.
- Final CTS status: finished.
- Final setup/hold WNS: 7.324 ns / 0.045 ns.
- `/usr/bin/time` wall time/RSS: 0:42.16 / 5,571,356 KB.

`ics55_huge_dev` with the mathematical solver:

- H-tree selected depth: 14.
- Solver status: `feasible_with_gap`.
- MILP size: 2184 variables, 324 binaries, 1860 continuous variables, 15032 constraints.
- Solver time: 57.788 s.
- Solver objective/gap: 2.120565 / 0.112700.
- Final CTS status: finished.
- Final setup/hold WNS: 3.487 ns / 0.076 ns.
- H-tree buffers/final clock buffers: 14248 / 44759.
- Total clock network wirelength: 850658.237 um.
- CTS optimization legality: cap legal true, slew legal true, slew violation count 0.
- CTS optimization skew: optimized skew 0.0902 ns, target 0.0800 ns, target not met.
- Final setup/hold worst skew: 0.089 ns / -0.070 ns.
- CTS internal elapsed time: 574.141 s, where H-tree build is 59.939 s,
  optimization is 436.505 s, and final Evaluation is 56.768 s.
- `/usr/bin/time` wall time/RSS: 10:19.80 / 21,749,964 KB.

Native huge comparison from `result_current_migrated_native_fix_20260526_1343`:

- Native H-tree selected depth: 14.
- Native H-tree build time: 232.789 s.
- Native final CTS status: finished.
- Native final setup/hold WNS: 3.492 ns / 0.071 ns.
- Native H-tree buffers/final clock buffers: 14418 / 44929.
- Native total clock network wirelength: 858294.798 um.
- Native CTS optimization legality: cap legal true, slew legal true, slew violation count 0.
- Native CTS optimization skew: optimized skew 0.0983 ns, target 0.0800 ns, target not met.
- Native CTS internal elapsed time: 534.871 s, where optimization is 227.067 s
  and final Evaluation is 54.517 s.
- Native `/usr/bin/time` wall time/RSS: 9:42.18 / 73,905,232 KB.
