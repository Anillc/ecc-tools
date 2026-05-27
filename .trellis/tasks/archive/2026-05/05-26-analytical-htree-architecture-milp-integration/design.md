# Design: H-tree architecture and MILP solver integration

## Current Structure

The current H-tree flow is implemented as one large orchestration method in
`HTree.cc`.

The effective sequence is:

1. Validate root net, loads, design, STA adapter, reporter, FastSTA, and DBU.
2. Build the topology through `TopologyGen`.
3. Run H-tree characterization and construct `CharBuilder` data.
4. Resolve boundary constraints, level plans, and depth candidates.
5. Build a `BufferPatternLibrary`.
6. If discrete mode, synthesize segment frontiers.
7. Build root-driver compensation and sink-load legality inputs.
8. If analytical mode, call `SelectAnalyticalHTreeSolution(...)` and return.
9. If discrete mode, run depth search, sink-load coverage, global selection, root
   compensation, selected-pattern materialization, root-driver sizing, embedding,
   and summary reporting.

`SelectAnalyticalHTreeSolution(...)` mirrors the discrete end of the flow after a
candidate is selected. This means selected-pattern application, root-driver
sizing, embedding, and final summary are present in both discrete and analytical
paths.

## Main Structural Problem

Analytical is currently not a peer algorithm. It is an early branch inside the
discrete build pipeline, but it has to reconstruct discrete result structures so
that the later CTS flow can continue. This creates three concrete costs:

- The H-tree root orchestration knows too much about both algorithms.
- Analytical and discrete finalization can drift because they duplicate code.
- Solver/backend concerns are mixed with H-tree selection concerns.

## Recommended Architecture

Keep `HTree` as the public flow boundary, but split the internals into three
stages:

```text
HTree::build
  -> HTreeSynthesisContextBuilder
  -> HTreeSelector (discrete or analytical)
  -> HTreeSolutionFinalizer
```

### Common Context

Add a common context type under `source/flow/synthesis/htree`, likely in a
`context/` or `pipeline/` subfolder:

```text
HTreeSynthesisContext
  input/config references
  topology
  diagnostics
  reporter stage handles or report sinks
  CharBuilder and characterization facts
  base/search boundary constraints
  full level plans
  depth candidates
  segment pattern library
  optional segment frontier catalog
  root-driver compensation input
  sink-load legality input
  fanout pruning config
```

The context builder owns the shared preparation work. It should not choose
discrete or analytical behavior beyond preparing data that both can consume.

### Selection Contract

Both algorithms should return a single shared selection contract:

```text
HTreeSelectedSolution
  selected = true/false
  failure_reason
  selection_engine = discrete | analytical
  selected_depth
  selected_evaluation
  selected_summary
  selected_compensation_detail
  selected_sink_load_region_legality
  topology_pattern_library
  best_char
  diagnostics/status fields
```

The selected contract should be close to the data already consumed by the
current finalization code: `CandidateBuildEvaluation`, `DepthSummary`,
`RootDriverCompensationDetail`, and `SinkLoadRegionLegalitySummary`.

### Discrete Selector

`DiscreteHTreeSelector` should own:

- required segment frontier resolution,
- segment frontier synthesis,
- depth candidate search,
- sink-load region coverage filtering,
- delay/power Pareto selection,
- selected root-driver compensation resolution,
- discrete selection diagnostics.

It returns `HTreeSelectedSolution` and does not perform embedding.

### Analytical Selector

`AnalyticalHTreeSelector` should own:

- unit segment char collection,
- analytical model catalog construction,
- `MathHtreeProblem` build,
- MILP solve,
- selected solution materialization back to H-tree pattern IDs,
- exact analytical candidate legality validation,
- analytical selection diagnostics.

It returns the same `HTreeSelectedSolution` and does not perform embedding.

The mathematical model and H-tree pattern materialization should remain
H-tree-specific. The external MILP API should not appear in this selector's
public header.

### Shared Finalizer

`HTreeSolutionFinalizer` should own the code currently duplicated between
discrete and analytical paths:

- copy selected best char/pattern into `HTree::Build`,
- set selected diagnostics,
- call `ApplySelectedPatternToLevelPlans`,
- resolve selected root-driver cell master,
- validate and apply root-driver sizing,
- build embedding,
- emit `LogSynthesisSummary`,
- finish/fail the build stage.

The finalizer should take `HTreeSynthesisContext` and `HTreeSelectedSolution`.

## Solver Adapter Design

The H-tree mathematical model should stay under:

```text
src/operation/iCTS/source/flow/synthesis/htree/analytical_solver/model
```

The solver backend should move behind a narrow facade. There are two viable
placements:

### Option A: Generic iCTS MILP module

```text
src/operation/iCTS/source/module/optimization/milp
  MilpModel.hh/.cc
  MilpSolver.hh/.cc
  MilpSolution.hh
  HighsMilpSolver.hh/.cc
```

Pros:
- external solver dependencies are not owned by H-tree flow code,
- reusable for future CTS mathematical modules,
- clearer dependency direction: H-tree flow depends on module.

Cons:
- more initial refactor work,
- requires designing a generic enough model contract.

### Option B: H-tree-local solver facade

```text
htree/analytical_solver/solver
  MathHtreeSolver.hh
  HighsMathHtreeSolver.hh/.cc
  SolverFactory.hh/.cc
```

Pros:
- smaller first implementation,
- keeps blast radius inside H-tree.

Cons:
- less reusable,
- external solver integration remains mixed with H-tree-specific model concepts.

Recommended path: implement Option B first only if schedule risk is high.
Otherwise implement Option A with a minimal generic MILP core, then keep
`MathHtreeProblem -> MilpModel` conversion under H-tree.

## Solver Backend Research Summary

### OR-Tools

OR-Tools supports C++ MIP modeling through `MPSolver` and recommends SCIP for
mixed integer problems. The local package exposes `ortools::ortools`, but the
imported target brings in Abseil, Protobuf, RE2, CBC/CLP, HiGHS, Eigen, and SCIP
targets. The repository already builds its own `src/third_party/abseil-cpp`.
That creates a realistic target collision/dependency contamination risk.

Decision: do not add OR-Tools as a submodule or runtime dependency. The
temporary OR-Tools-packaged SCIP executable adapter was useful for comparison
only and is removed from production code.

### HiGHS

HiGHS supports LP, MIP, and QP, is C++11, MIT-licensed, and source builds require
CMake with no other third-party utilities according to the official project
page. Its C++ API uses `HighsModel`/`HighsLp`, supports integrality flags, and
returns model status and solve info.

Decision: accepted as the production backend and vendored directly as ordinary
source under `src/third_party/highs`. iCTS links only the static `highs` target.
The H-tree-local adapter builds `HighsModel` directly from `MathHtreeProblem`,
maps HiGHS model/solution status to `MathHtreeSolveStatus`, and collects
selected binary values plus continuous timing/power variables from
`HighsSolution`.

### SCIP Direct API

SCIP is strong for MILP and has a C/C++ API, but direct integration still brings
SCIP and SoPlex dependency management. SCIP license is Apache 2.0 since 8.0.3,
but the full Optimization Suite can involve extra dependency licenses depending
on build flags.

Recommendation: second in-process candidate if HiGHS is insufficient.

### CBC / COIN-OR

CBC is an open-source C++ MIP solver and callable library, but it relies on
other COIN-OR components such as OSI, CLP, and CGL. The local OR-Tools package
already includes these libraries, but the main repository does not currently
wire system CBC/CLP into iCTS.

Recommendation: possible fallback, lower priority than HiGHS.

### Existing LEMON

The repository contains LEMON and LEMON has LP/MIP wrapper interfaces, but the
checked-in `lemon/config.h` has `LEMON_HAVE_LP` and `LEMON_HAVE_MIP` undefined.
LEMON does not solve MILPs by itself; it needs GLPK, CBC, CPLEX, etc.

Recommendation: not a production backend for this task.

### GLPK

GLPK is comparatively light and common, but its license and expected MILP
runtime make it a poor default for this repository unless explicitly approved.

Recommendation: reject for production first pass.

## Compatibility

- Discrete remains default.
- Analytical remains enabled by CTS config.
- Analytical MILP uses the HiGHS backend only, and the selected backend remains
  reportable as `HiGHS in-process`.
- HiGHS is provided as ordinary vendored source under `src/third_party/highs` so
  a normal repository checkout has a usable solver without submodule
  initialization.
- Existing result report fields should remain stable where possible.
- OR-Tools and external-SCIP runtime configuration are not part of production
  compatibility.

## Risks

- Refactoring finalization can introduce subtle behavior changes in root-driver
  sizing or embedding.
- Solver adapter abstraction may overgeneralize if designed too broadly.
- HiGHS may solve the MILP differently from the temporary SCIP comparison
  backend; final QoR and downstream optimizer interaction still need real-design
  validation before finish.
- OR-Tools source/submodule integration was rejected because of its broad
  dependency graph and existing Abseil usage.
