# Implementation plan

## Phase 1: Planning Review

- [x] Create the Trellis task.
- [x] Inspect current H-tree/discrete/analytical structure.
- [x] Inspect repository third-party organization.
- [x] Inspect local OR-Tools package dependency shape.
- [x] Research official OR-Tools, HiGHS, SCIP, and CBC capabilities.
- [x] Write initial `prd.md`, `design.md`, `implement.md`, and `research.md`.
- [x] Get user confirmation on first backend priority:
  - initial spike: HiGHS first, compare against external SCIP.
- [x] After real-design comparison, get user direction to remove OR-Tools/SCIP
      configuration and keep HiGHS as the only MILP backend.

Do not start implementation until this review is complete.

## Phase 2: H-tree Structural Refactor

- [x] Add shared selected-solution contract for H-tree selection.
- [ ] Add common H-tree synthesis context contract.
- [ ] Extract shared preparation from `HTree.cc` into context builder code.
- [x] Extract discrete segment frontier/depth-search/global-selection into
      `DiscreteHTreeSelector`.
- [x] Extract analytical solve/validation selection into `AnalyticalHTreeSelector`.
- [x] Extract selected-solution finalization into shared finalizer.
- [x] Remove duplicated finalization from `solution/analytical/AnalyticalSolution.cc`.
- [ ] Keep `HTree.cc` as a small orchestration boundary:

```text
prepare context -> select engine -> finalize selected solution
```

## Phase 3: Solver Backend Facade

- [x] Define a narrow solver interface independent from external solver APIs.
- [ ] Decide placement:
  - preferred: `source/module/optimization/milp`,
  - smaller fallback: `htree/analytical_solver/solver`.
- [x] Temporarily move current external-SCIP execution behind the new facade for
      comparison.
- [x] Remove `/home/liweiguo/...` hardcoded production fallback.
- [x] Remove the external-SCIP facade path after HiGHS validation.
- [x] Remove runtime backend selection and OR-Tools/SCIP environment variables
      from production code.

## Phase 4: In-process Backend Spike

Recommended first spike: HiGHS.

- [x] Create a tiny standalone CMake probe outside iCTS or as a guarded test to
      compile and solve a binary MILP with HiGHS.
- [x] Verify the backend can report:
  - optimal/infeasible/unbounded/time-limit status,
  - incumbent objective,
  - objective bound or MIP gap,
  - selected binary values,
  - wall time.
- [x] If HiGHS works, implement a guarded backend adapter.
- [x] Reject direct OR-Tools linking/submodule because of dependency weight and
      target-conflict risk.
- [x] Defer direct SCIP C API; HiGHS is sufficient for the current production
      backend.

HiGHS integration result:

- [x] Vendored HiGHS as ordinary source under `src/third_party/highs` from
      v1.11.0 (`364c83a51e44ba6c27def9c8fc1a49b1daf5ad5c`) so users can build
      without submodule initialization.
- [x] Added scoped `src/third_party/CMakeLists.txt` wiring that builds only the
      static `highs` library target.
- [x] Added `HighsMathHtreeSolver.hh/.cc` with direct `HighsModel` construction.
- [x] Made `MathHtreeMilpSolver` a HiGHS-only facade.
- [x] Deleted `ScipMathHtreeSolver.hh/.cc` and removed it from CMake.

## Phase 5: Tests

- [ ] Unit test the shared selected-solution finalizer with discrete-like and
      analytical-like selected solutions.
- [x] Unit test MILP model conversion on a tiny deterministic problem through
      the existing analytical H-tree solver cases.
- [x] Unit test backend status mapping for at least:
  - optimal.
- [x] Keep the existing analytical solver test passing.

## Phase 6: Build and Real-design Validation

Focused build:

```bash
cmake --build build --target \
  icts_source_flow_synthesis_htree \
  icts_source_flow_synthesis_htree_solution \
  icts_source_flow_synthesis_htree_analytical_solver \
  icts_test_flow_synthesis_htree_analytical_solver -- -j 16
```

Focused test:

```bash
ctest --test-dir build -R '^icts_test_flow_synthesis_htree_analytical_solver$' --output-on-failure
```

Real designs:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl

cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_huge_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Final checker only after implementation and real-design validation:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Current implementation status:

- [x] Focused H-tree/solution/analytical-solver build passed after HiGHS
      integration.
- [x] Analytical solver ctest passed with no configured external solver; success
      paths use the HiGHS backend.
- [x] Analytical solver ctest was run against the local OR-Tools SCIP package
      before the external backend was removed.
- [x] The temporary backend-selection environment switch was removed after the
      comparison; production analytical H-tree always uses HiGHS.
- [x] Real-design validation comparing HiGHS and OR-Tools packaged SCIP was run
      on `ics55_dev` and `ics55_huge_dev` using analytical H-tree config.
      Result directories:
      - `scripts/design/ics55_dev/result_compare_highs_20260527`
      - `scripts/design/ics55_dev/result_compare_scip_20260527`
      - `scripts/design/ics55_huge_dev/result_compare_highs_20260527`
      - `scripts/design/ics55_huge_dev/result_compare_scip_20260527`
- [x] Validation summary:
      - `ics55_dev`: HiGHS optimal in 21.508 s solver time; SCIP optimal in
        9.055 s solver time. Final QoR matched: 2998 final clock buffers,
        10122.560 um^2 buffer area, 61639.259 um total clock wirelength,
        setup WNS 7.323619 ns, hold WNS 0.044602 ns.
      - `ics55_huge_dev`: HiGHS optimal in 33.865 s solver time with 0.0001
        gap; SCIP returned feasible_with_gap in 57.564 s with 0.1065 gap.
        HiGHS selected lower validated delay (0.568093 ns vs 0.590393 ns) but
        higher validated power (0.041453 W vs 0.040219 W). Final HiGHS QoR:
        44743 buffers, 139162.800 um^2 area, setup/hold WNS 3.500311/0.067750
        ns, setup/hold skew 0.069/-0.060 ns. Final SCIP QoR: 44823 buffers,
        130394.320 um^2 area, setup/hold WNS 3.500697/0.071139 ns,
        setup/hold skew 0.095/-0.062 ns.
      - Huge-design total CTS runtime was dominated by downstream optimization:
        HiGHS total 822.674 s with 707.377 s in optimization, SCIP total
        338.391 s with 201.258 s in optimization. The solver-only comparison is
        therefore more favorable to HiGHS than the full-flow runtime number.
- [x] Final full iCTS checker passed after direct HiGHS vendoring:
      `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`
      reported 0 in-scope findings.

## Risky Files

- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/solution/analytical/AnalyticalSolution.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/solution/report/SolutionReport.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/solution/selection/SolutionSelection.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/analytical_solver`
- `src/operation/iCTS/source/module/CMakeLists.txt`
- `src/third_party/CMakeLists.txt` and `src/third_party/highs` vendored solver
  source

## Rollback Points

- HiGHS is now the production backend. The external SCIP comparison backend has
  been removed, so rollback means restoring it from the previous task diff if a
  future validation regression requires another backend comparison.
- Keep discrete H-tree selected-solution behavior byte-for-byte or metric-equivalent
  before changing analytical backend.
- If solver integration causes build contamination, revert to external executable
  backend plus explicit configuration and defer in-process integration.
- If finalizer extraction changes QoR, first restore discrete behavior, then port
  analytical onto the corrected shared finalizer.
