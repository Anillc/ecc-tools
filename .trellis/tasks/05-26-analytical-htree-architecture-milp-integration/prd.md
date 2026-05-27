# Refactor analytical H-tree architecture and MILP integration

## Goal

Refactor the CTS H-tree synthesis structure so discrete H-tree and analytical
H-tree are represented as sibling algorithmic selection engines behind a shared
H-tree synthesis contract. At the same time, replace the current ad hoc
external-SCIP executable binding with a maintainable MILP backend adaptation
layer and decide whether OR-Tools, HiGHS, SCIP, CBC, or another open-source
solver should be integrated into the project.

This task is complete. The final implementation prioritizes the H-tree
structural split, introduces an H-tree-local MILP facade, and uses `discrete`
instead of the previous abstract `native` naming for the characterized frontier
search engine. After real-design validation, the solver integration decision is
HiGHS-only: HiGHS is vendored as ordinary source under `src/third_party/highs`
and the previous OR-Tools-packaged SCIP compatibility backend is removed from
production code.

## User Value

The current mathematical analytical H-tree path works, but it was added on top
of the discrete flow under time pressure. The result is functional but structurally
uneven: analytical selection is embedded as a special early-return path, final
materialization and reporting are duplicated, and the MILP backend depends on a
developer-local OR-Tools SCIP executable fallback. This task should make the H-tree
algorithm boundary explicit, make discrete and analytical easier to compare and
extend, and make the external solver dependency reliable for future users.

## Confirmed Facts From Code Inspection

- Discrete H-tree remains orchestrated inside `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`.
- `HTree.cc` currently performs topology generation, characterization, boundary
  setup, segment frontier synthesis, analytical dispatch, discrete depth search,
  global selection, root-driver compensation, selected-pattern application,
  embedding, and summary reporting in one build method.
- Analytical mode skips discrete segment frontier synthesis and calls
  `htree::analytical_solution::SelectAnalyticalHTreeSolution(...)`.
- Analytical solution logic duplicates important finalization steps from discrete:
  selected evaluation transfer, selected pattern materialization,
  `ApplySelectedPatternToLevelPlans`, root-driver sizing validation/application,
  `BuildEmbedding`, and `LogSynthesisSummary`.
- `analytical_solver` has a mathematical model boundary:
  `MathHtreeProblem`, `MathHtreeSolution`, `MathHtreeProblemBuilder`, and
  `MathHtreeMilpSolver`.
- The previous external SCIP backend wrote LP files, ran an external `scip`
  executable with `posix_spawn`, and parsed log/solution files. It was retained
  for backend comparison, then removed after HiGHS validation.
- The repository already has `src/third_party`, including LEMON, but the bundled
  LEMON config has LP/MIP support disabled and does not provide an actual MILP
  backend by itself.
- The local OR-Tools package contains `libortools`, SCIP, HiGHS, CBC/CLP, Abseil,
  Protobuf, RE2, Boost, and CMake package files. Its `ortools::ortools` imported
  target pulls a broad dependency graph.

## Requirements

- Preserve current user-visible behavior by default: discrete H-tree remains the
  default unless CTS config enables analytical H-tree.
- Do not add an analytical compile-time macro. Runtime selection stays driven by
  CTS config.
- Split H-tree synthesis into common preparation, algorithmic selection, and
  shared finalization.
- Make discrete and analytical selection engines consume the same prepared
  context and return the same selected-solution contract.
- Remove duplicated finalization logic between discrete and analytical paths.
- Keep discrete-specific frontier/depth-search code out of analytical-specific
  code, and keep analytical MILP/model code out of discrete-specific code.
- Keep H-tree-specific mathematical model construction under H-tree ownership.
- Put MILP solver adapter code behind a narrow facade so HiGHS APIs do not leak
  into H-tree algorithm code.
- Support clear solver status reporting: optimal, feasible with gap, timeout,
  infeasible, unbounded, unavailable, model invalid, abnormal/numerical failure.
- Eliminate developer-local hardcoded solver paths from production behavior.
- Use HiGHS as the only production MILP backend. Do not keep OR-Tools, external
  SCIP executable paths, or runtime backend-selection configuration in
  production code.
- Vendor HiGHS directly under `src/third_party/highs`, using the
  real-design-validated v1.11.0 source commit, so users can build from a normal
  repository checkout without submodule initialization.
- Use `discrete` for the characterized frontier/depth-search H-tree engine in
  production code and reports; reserve `native` only for historical discussion.
- Keep the first committed architecture at `HTree.cc` common preparation plus
  explicit analytical/discrete selection dispatch and shared finalization. A
  full `HTreeSynthesisContextBuilder` extraction is intentionally deferred to a
  later, lower-risk refactor.
- Research and document OR-Tools integration difficulty, including whether
  adding it under `src/third_party` or as a git submodule is too heavy for this
  repository.
- Research and document lighter open-source alternatives, with priority on
  libraries that are buildable via CMake, have permissive licensing, expose a C
  or C++ API, and support MILP with incumbent/gap/status reporting.

## Preferred Technical Direction

The preferred architecture is:

- Keep `source/flow/synthesis/htree` as the H-tree flow boundary.
- Introduce a shared H-tree synthesis context and selected-solution contract.
- Move discrete depth-search/global-selection behind a `DiscreteHTreeSelector`.
- Move analytical model/solver/validation behind an `AnalyticalHTreeSelector`.
- Move selected pattern application, root-driver sizing, embedding, and summary
  emission into a shared finalizer.
- Introduce a solver backend facade. H-tree-specific `MathHtreeProblem` may stay
  under `htree/analytical_solver`, but the backend adapter should be either:
  - a generic `source/module/optimization/milp` module if the model contract is
    general enough, or
  - a strict `htree/analytical_solver/solver` facade if the implementation
    remains H-tree-specific in the first pass.

The final solver-adapter direction after research and real-design validation is:

1. Use an in-process HiGHS backend because HiGHS is smaller, CMake-based,
   MIT-licensed, and directly supports LP/MIP/QP through C/C++ APIs.
2. Treat direct OR-Tools integration as rejected because it cannot be added as a
   lightweight source dependency without bringing a broad dependency graph.
3. Remove the external SCIP executable backend after comparison because it
   requires runtime path/library configuration and returned weaker huge-design
   solver status than HiGHS.
4. Treat direct SCIP C API as deferred unless HiGHS fails future requirements.
5. Avoid GLPK for production unless explicitly approved because of license and
   performance risk.
6. Keep HiGHS under `src/third_party/highs` as ordinary vendored source and
   build only the C++ library target needed by iCTS; do not build HiGHS
   examples, tests, CLI, Python, CSharp, Fortran, CUDA, or zlib paths for this
   integration.
7. Do not rely on the existing LEMON target for MILP; it is only a wrapper layer
   and currently has MIP support disabled.

## Acceptance Criteria

- [x] Planning documents describe the current H-tree decomposition, the proposed
      discrete/analytical split, and the solver integration decision matrix.
- [x] `HTree.cc` is reduced to orchestration; common preparation, selection, and
      finalization have clear boundaries.
- [x] Discrete and analytical selection paths implement the same selected-solution
      contract.
- [x] Shared finalization is used by both discrete and analytical paths without
      duplicating embedding/root-sizing/report logic.
- [x] Discrete H-tree remains default and keeps existing QoR behavior within
      expected variation.
- [x] Analytical H-tree remains opt-in through CTS config and still produces
      legal results on `ics55_dev`.
- [x] Huge-design validation compares discrete and analytical after the refactor.
- [x] MILP backend abstraction is implemented with the in-process HiGHS backend.
- [x] Production code contains no hardcoded developer-local solver path.
- [x] OR-Tools/external-SCIP production configuration and adapter code are
      removed.
- [x] HiGHS model conversion and solver status reporting are covered by focused
      integration tests; timeout option wiring is implemented in the backend.
- [x] Build/CMake changes do not make OR-Tools or other heavy solver dependencies
      mandatory; HiGHS is the accepted direct third-party solver dependency.
- [x] Final full iCTS checker passes:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Validation Targets

Focused build/test targets:

```bash
cmake --build build --target \
  icts_source_flow_synthesis_htree \
  icts_source_flow_synthesis_htree_solution \
  icts_source_flow_synthesis_htree_analytical_solver \
  icts_test_flow_synthesis_htree_analytical_solver -- -j 16

ctest --test-dir build -R '^icts_test_flow_synthesis_htree_analytical_solver$' --output-on-failure
```

Real-design validation:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl

cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_huge_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

## Out of Scope

- Changing the mathematical H-tree formulation objective unless required by the
  refactor.
- Replacing characterization fitting logic.
- Reworking FastSTA or iSTA behavior.
- Adding proprietary solver dependencies.
- Making analytical H-tree the default path.
- Running broad ECC dev checks during normal edit iterations; reserve the full
  iCTS checker for final validation.

## Product Decision

Use HiGHS as the only production MILP backend. The OR-Tools/SCIP executable path
was useful for comparison, but it is removed before commit so users can compile
analytical H-tree support directly from a normal repository checkout.
