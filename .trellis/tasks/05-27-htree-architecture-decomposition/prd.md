# H-tree architecture decomposition

## Goal

Decompose the current H-tree synthesis implementation so `HTree.cc` becomes a
thin flow boundary and the main internal stages have explicit contracts:

```text
validate / trivial handling
  -> assemble shared H-tree synthesis state
  -> select solution engine (discrete or analytical)
  -> finalize selected solution
```

The task should preserve existing discrete and analytical behavior while making
future analytical work easier to reason about, test, and compare against the
discrete path.

## User Value

The previous task made analytical and discrete selection closer to sibling
engines and introduced a shared finalizer. The remaining structural problem is
that `HTree.cc` still owns most shared build setup and passes long argument lists
into both selectors. This makes the H-tree flow harder to extend because
algorithm code, shared setup code, reporting, and build-state mutation are still
coupled through one large method.

This task should turn the current implicit build state into a named
CTS/H-tree-oriented synthesis contract, reduce selector signatures, and make the
decomposition visible in the directory/CMake structure.

## Confirmed Facts From Code Inspection

- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc` still performs:
  root-net validation, trivial single-load handling, topology generation,
  characterization, boundary constraint resolution, level/depth planning,
  segment pattern library construction, optional discrete segment-frontier
  synthesis, root-driver compensation input construction, sink-load legality
  input construction, analytical/discrete dispatch, and finalizer invocation.
- `solution/discrete/DiscreteSolution.*` owns discrete depth search, sink-load
  coverage filtering, global Pareto selection, selected legality resolution, and
  selected root-driver compensation evaluation.
- `solution/analytical/AnalyticalSolution.*` owns analytical MILP selection,
  analytical diagnostics transfer, and selected-solution construction.
- `solution/finalization/SolutionFinalizer.*` already owns shared selected
  pattern materialization, root-driver sizing validation/application, embedding,
  and synthesis summary reporting.
- `HTreeSelectedSolution` is already common, but `DiscreteHTreeSelectionBuild`
  and `AnalyticalHTreeSelectionBuild` are duplicated wrappers with the same
  shape.
- Both selector entry points still take long parameter lists that are logically
  one H-tree synthesis state.
- `SegmentFrontierCatalog` is needed by discrete only; analytical explicitly
  skips segment frontier synthesis because it uses unit affine models.
- `BufferPatternLibrary` has no default constructor because it requires
  `STAAdapter`; it should be owned by a move-only synthesis state or wrapped
  in `std::optional`.
- `CharacterizationLibrary` may be provided by the caller or built locally; a
  synthesis state must preserve local library lifetime without storing
  pointers that become invalid after moves.
- `SchemaWriter::StageScope` is move-only and is best kept in the H-tree
  orchestration layer, passed by reference only when state construction/finalization
  needs to finish or fail the build stage.
- Existing backend specs keep `source/flow/synthesis/htree` as the H-tree
  topology implementation boundary and allow helpers under subdirectories.
- Previous task deferred the common state/state-builder extraction as a
  lower-risk follow-up.

## Requirements

- Preserve external `HTree::build(const Input&, const Config&)` behavior and
  public `HTree::Input`, `HTree::Config`, `HTree::Output`, and `HTree::Summary`
  shape unless a required internal-only adjustment is explicitly justified.
- Keep discrete H-tree as the default path and analytical H-tree opt-in through
  `Config::enable_analytical_solver`.
- Do not modify the mathematical formulation, HiGHS backend, third-party source,
  CTS config schema, or downstream optimization behavior in this task.
- Extract a named H-tree synthesis-state contract that owns or references all
  data shared by discrete and analytical selection.
- Move shared synthesis-state construction out of `HTree.cc` without moving
  algorithmic selection work into the shared-state layer.
- Change discrete and analytical selection entry points to consume the common
  synthesis state instead of long argument lists.
- Prefer one common selection-result wrapper over duplicate analytical/discrete
  wrappers when it reduces coupling without obscuring engine-specific logic.
- Keep selected-solution finalization shared and update it to consume the common
  synthesis state where practical.
- Use the stronger segment-frontier ownership split: discrete selector owns
  discrete-only required segment-frontier resolution and synthesis; analytical
  selector does not see or depend on `SegmentFrontierCatalog`.
- Keep root-driver compensation, sink-load legality, boundary constraints,
  pattern libraries, and reporting stage behavior equivalent to the current
  implementation.
- Keep H-tree-specific code under `src/operation/iCTS/source/flow/synthesis/htree`.
- Do not update `.trellis/spec` unless this work discovers a genuine global
  development convention; task-local architecture notes belong in task docs.

## Acceptance Criteria

- [ ] `HTree.cc` is reduced to thin orchestration: early validation/trivial
      handling, synthesis-state construction, selection dispatch, finalization,
      and final production-build extraction.
- [ ] A common H-tree synthesis-state contract exists and is used by both
      discrete and analytical selection paths.
- [ ] Selector entry-point signatures no longer duplicate the same long
      synthesis-state argument list.
- [ ] Discrete-only segment frontier synthesis is owned by the discrete selector
      with an explicit dependency boundary.
- [ ] Analytical-only MILP/model code remains under `analytical_solver` and does
      not leak HiGHS types into H-tree flow or solution public headers.
- [ ] Shared finalization remains the only place that applies selected patterns,
      validates/applies root-driver sizing, builds embedding, and emits the
      final synthesis summary.
- [ ] Existing focused H-tree and analytical solver tests pass.
- [ ] Focused H-tree build targets pass.
- [ ] Final full iCTS checker passes before commit.

## Validation Targets

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

Final checker:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Real-design validation:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl

cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_huge_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

## Out of Scope

- Changing analytical MILP objective, formulation, or HiGHS adapter behavior.
- Pruning or changing `src/third_party/highs`.
- QoR/runtime tuning beyond verifying that the architecture refactor preserves
  legal behavior on `ics55_dev` and `ics55_huge_dev`.
- Adding new CTS config options.
- Reworking FastSTA/iSTA behavior.
- Moving H-tree code out of `source/flow/synthesis/htree`.

## Open Scope Decision

- Resolved: use `HTreeSynthesisState` / `SynthesisState.hh` under a
  `synthesis_state/` subdirectory, replacing generic `preparation` / `context`
  terminology.
