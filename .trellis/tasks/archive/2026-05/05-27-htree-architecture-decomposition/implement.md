# Implementation plan

## Phase 1: Planning Review

- [x] Create Trellis task.
- [x] Inspect current H-tree directory structure.
- [x] Inspect `HTree.cc` orchestration responsibilities.
- [x] Inspect discrete selector, analytical selector, and shared finalizer.
- [x] Inspect shared-state-adjacent contracts: characterization, constraints,
      plan, segment pruning, root-driver compensation, sink-load legality.
- [x] Review prior archived task notes for deferred architecture work.
- [x] Draft `prd.md`, `design.md`, and `implement.md`.
- [x] Get user decision on implementation scope before running `task.py start`.

## Phase 2: Shared Synthesis State Extraction

- [x] Confirm naming:
      `synthesis_state/SynthesisState.hh/.cc`,
      `HTreeSynthesisState`, `HTreeSynthesisStateBuild`, and
      `AssembleHTreeSynthesisState`.
- [x] Add the selected shared-state subdirectory.
- [x] Add selected `.hh/.cc` files with required copyright/Doxygen headers.
- [x] Add the selected shared-state CMake target.
- [x] Add the selected status enum, state contract, and state-build result.
- [x] Implement safe accessors for caller-owned vs local `CharacterizationLibrary`.
- [x] Store `BufferPatternLibrary` with safe initialization semantics.
- [x] Move shared non-trivial synthesis-state assembly out of `HTree.cc`:
      topology generation, characterization, constraints, level/depth planning,
      segment pattern library construction, root-driver compensation input,
      fanout pruning config, and sink-load legality input.
- [x] Do not include `SegmentFrontierCatalog` in the shared state.
- [x] Preserve trivial root/no-load/single-load behavior exactly.
- [x] Keep `SchemaWriter::StageScope` owned by `HTree.cc` and pass it by
      reference into state assembly.

## Phase 3: Selector Contract Cleanup

- [x] Add or relocate common `HTreeSelectionBuild`.
- [x] Change `SelectDiscreteHTreeSolution` to take the selected shared
      synthesis-state type.
- [x] Change `SelectAnalyticalHTreeSolution` to take the selected shared
      synthesis-state type.
- [x] Replace duplicate analytical/discrete selection build wrappers where
      practical.
- [x] Optionally add `SelectHTreeSolution(...)` as the central dispatch helper.
- [x] Move required segment-frontier resolution and synthesis into
      `solution/discrete`; preserve current `HTree/Synthesize segment frontiers`
      report semantics as much as practical.
- [x] Keep discrete-only depth search, sink-load coverage, global selection, and
      selected compensation inside `solution/discrete`.
- [x] Keep analytical-only MILP/model/validation diagnostics inside
      `solution/analytical` and `analytical_solver`.

## Phase 4: Finalizer Contract Cleanup

- [x] Change `FinalizeSelectedHTreeSolution` to consume
      the selected shared synthesis-state type plus `StageScope&` and selected
      solution.
- [x] Preserve selected pattern materialization behavior.
- [x] Preserve root-driver sizing precheck and application behavior.
- [x] Preserve embedding and summary report behavior.
- [x] Verify `HTree.cc` no longer passes a long finalization argument list.

## Phase 5: HTree.cc Slimming

- [x] Reduce `HTree.cc` to public entry, initial validation/trivial handling,
      state assembly call, selection call, finalization call, and
      production-build extraction.
- [x] Remove includes from `HTree.cc` that are no longer directly needed.
- [x] Verify no algorithmic search or solver detail remains in `HTree.cc`.

## Phase 6: Tests and Validation

Focused build:

```bash
cmake --build build --target \
  icts_source_flow_synthesis_htree \
  icts_source_flow_synthesis_htree_solution \
  icts_source_flow_synthesis_htree_analytical_solver \
  icts_test_flow_synthesis_htree \
  icts_test_flow_synthesis_htree_analytical_solver -- -j 16
```

Focused tests:

```bash
ctest --test-dir build -R '^icts_test_flow_synthesis_htree$|^icts_test_flow_synthesis_htree_analytical_solver$' --output-on-failure
```

Diff hygiene:

```bash
git diff --check
git diff --cached --check
```

Final checker before commit:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Real-design validation is required for this task:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl

cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_huge_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Validation results:

- [x] Focused build passed:
      `icts_source_flow_synthesis_htree`,
      `icts_source_flow_synthesis_htree_solution`,
      `icts_source_flow_synthesis_htree_analytical_solver`,
      `icts_test_flow_synthesis_htree`,
      `icts_test_flow_synthesis_htree_analytical_solver`.
- [x] Focused tests passed:
      `icts_test_flow_synthesis_htree`,
      `icts_test_flow_synthesis_htree_analytical_solver`.
- [x] `iEDA` target rebuilt successfully.
- [x] `ics55_dev` real-design validation passed:
      selected H-tree depth 11, H-tree buffers 806, final clock buffers 2996,
      setup/hold WNS 7.317 ns / 0.044 ns, total runtime 14.339 s.
- [x] `ics55_huge_dev` real-design validation passed:
      selected H-tree depth 14, H-tree buffers 14418, final clock buffers
      44929, setup/hold WNS 3.492 ns / 0.071 ns, total runtime 532.448 s.
- [x] Diff hygiene passed:
      `git diff --check`, `git diff --cached --check`.
- [x] Final checker passed:
      `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`.

## Risky Files

- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/CMakeLists.txt`
- `src/operation/iCTS/source/flow/synthesis/htree/solution/CMakeLists.txt`
- `src/operation/iCTS/source/flow/synthesis/htree/solution/Solution.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/solution/discrete/DiscreteSolution.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/solution/discrete/DiscreteSolution.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/solution/analytical/AnalyticalSolution.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/solution/analytical/AnalyticalSolution.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/solution/finalization/SolutionFinalizer.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/solution/finalization/SolutionFinalizer.cc`

## Rollback Points

- If shared-state extraction introduces lifetime issues, keep the new state type
  but return to passing explicit references until lifetimes are resolved.
- If selector signature cleanup causes broad churn, complete shared-state
  extraction first and defer common `HTreeSelectionBuild` unification.
- If finalizer state consumption changes behavior, restore the old finalizer
  signature while keeping the shared state for selectors.
- If CMake target extraction creates a dependency cycle, split data-only state
  declarations from state-construction implementation.

## Review Gate

Do not start implementation until the user approves the decomposition scope.
