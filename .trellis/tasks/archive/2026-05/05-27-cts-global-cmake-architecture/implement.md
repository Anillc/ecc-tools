# CTS Global CMake Architecture Remediation Checklist

## Execution Rules

- Do not commit this task after implementation; leave the final diff for user review.
- Keep this checklist current. Mark an item complete only after the corresponding cleanup is implemented and the relevant validation passes.
- Keep dependencies target-based; never reintroduce `${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/lib*.a` or generated archive path links.
- Prefer concrete target links over broad aggregate targets inside implementation libraries.
- Default to `PRIVATE`; use `PUBLIC` only when the public header requires the dependency.
- Run `ics55_dev` before the final ecc dev check.
- Run the final ecc dev check only after `ics55_dev` passes.

## Phase 0: Baseline And Safety

- [x] Confirm the working tree only contains this task's planning files before implementation starts.
- [x] Start the task with `python3 ./.trellis/scripts/task.py start 05-27-cts-global-cmake-architecture` after user review approval.
- [x] Read required specs via `trellis-before-dev` before editing:
  - `.trellis/spec/project-constraints.md`
  - `.trellis/spec/backend/directory-structure.md`
  - `.trellis/spec/backend/quality-guidelines.md`
- [x] Record current CMake baseline:
  - [x] Count CTS `CMakeLists.txt` files.
  - [x] Count `add_library()` targets.
  - [x] Count broad include-root exposures.
  - [x] Count aggregate target references.
- [x] Confirm archive path cleanup remains clean:

```bash
rg -n "CMAKE_ARCHIVE_OUTPUT_DIRECTORY|\\.a\\b|libicts_.*\\.a" src/operation/iCTS -g 'CMakeLists.txt' || true
```

## Phase 1: Synthesis Public Contract Cleanup

Goal: reduce public header dependency width before changing CMake ownership.

- [x] Audit `source/flow/synthesis/htree` public headers.
  - [x] `HTree.hh`
  - [x] `synthesis_state/SynthesisState.hh`
  - [x] `plan/DepthPlan.hh`
  - [x] `solution/Solution.hh`
  - [x] `analytical_solver/AnalyticalSolver.hh`
  - [x] `segment_pruning/*Library.hh`
- [x] Audit `source/flow/synthesis/topology` public headers.
  - [x] `Topology.hh`
  - [x] `sink/SinkBranch.hh`
  - [x] `sink/SinkLoadClustering.hh`
  - [x] `trunk/SourceTrunk.hh`
  - [x] `trunk/SourceTrunkSegment.hh`
  - [x] `buffer/BufferInsertion.hh`
- [x] Audit `source/flow/synthesis/trace` public headers.
  - [x] `SynthesisTrace.hh`
  - [x] `layout/ClockLayoutAdapter.hh`
  - [x] `distance/TopologyDistanceReport.hh`
  - [x] `topology_build/TopologyBuildTrace.hh`
- [x] Move implementation-only includes from headers to `.cc` files where forward declarations are enough.
- [x] Keep every touched header self-contained.
- [x] Rebuild the touched targets after this phase.

Validation:

```bash
cmake --build build --target icts_source_flow_synthesis_htree icts_source_flow_synthesis_topology icts_source_flow_synthesis_trace -- -j 16
```

## Phase 2: Synthesis CMake Ownership Cleanup

Goal: make H-tree, topology, and trace depend on facades and concrete contracts, not peer implementation details.

- [x] Classify synthesis targets as external facade or private implementation detail.
  - [x] `icts_source_flow_synthesis_htree`
  - [x] `icts_source_flow_synthesis_htree_*`
  - [x] `icts_source_flow_synthesis_topology`
  - [x] `icts_source_flow_synthesis_topology_*`
  - [x] `icts_source_flow_synthesis_trace`
  - [x] `icts_source_flow_synthesis_trace_*`
- [x] Make subfolder implementation targets private to the nearest facade where possible.
- [x] Remove direct higher-level links to H-tree implementation detail targets where a facade can own them.
- [x] Remove direct higher-level links to trace/layout implementation detail targets where a facade can own them.
- [x] Preserve the current split of `htree_plan` and `htree_depth_plan` unless a cleaner target boundary is proven.
- [x] Re-check `PUBLIC` / `PRIVATE` visibility for all touched synthesis links.
- [x] Ensure no H-tree/topology/trace circular dependency is hidden by broad aggregate links.

Validation:

```bash
cmake --build build --target icts_source_flow_synthesis icts_source_flow_synthesis_htree icts_source_flow_synthesis_topology icts_source_flow_synthesis_trace -- -j 16
ctest --test-dir build -R '^icts_test_flow_synthesis_htree$|^icts_test_flow_synthesis_htree_analytical_solver$' --output-on-failure
```

## Phase 3: Module And Database Concrete Dependency Cleanup

Goal: replace broad module/database aggregation with concrete target ownership where practical.

- [x] Audit remaining implementation-target uses of `icts_source_database`.
- [x] Replace `icts_source_database` with concrete database targets where the target uses only a narrow domain:
  - [x] `icts_source_database_design`
  - [x] `icts_source_database_config`
  - [x] `icts_source_database_spatial`
  - [x] `icts_source_database_routing`
  - [x] `icts_source_database_timing`
  - [x] `icts_source_database_characterization`
  - [x] `icts_source_database_qor`
  - [x] `icts_source_database_adapter_*`
- [x] Keep `icts_source_database` only at true boundary or facade aggregation points where a broad database contract is intentional.
- [x] Audit remaining implementation-target uses of `icts_source_utils`.
- [x] Replace `icts_source_utils` with concrete utility targets where practical:
  - [x] `icts_source_utils_logger`
  - [x] `icts_source_utils_geometry`
  - [x] `icts_source_utils_visualization`
- [x] Keep `icts_source_utils` only at true boundary or facade aggregation points where a broad utility contract is intentional.
- [x] Confirm `icts_source_module` is not used by internal implementation targets.
- [x] Re-check `source/module/characterization`, `source/module/routing`, `source/module/topology`, and `source/database/adapter/fast_sta` target visibility.

Validation:

```bash
cmake --build build --target iEDA -- -j 16
```

## Phase 4: Include Root Tightening

Goal: reduce broad include-root exposure without breaking self-contained headers.

- [x] Audit `${ICTS_SOURCE}` exposures and remove those that are not required by rooted public includes.
- [x] Audit `${ICTS_FLOW}` exposures and narrow them when only a subdirectory facade is needed.
- [x] Audit `${ICTS_MODULE}` exposures and narrow them to concrete module roots where possible.
- [x] Audit `${ICTS_DATABASE}` exposures and narrow them to concrete database roots where possible.
- [x] Audit `${ICTS_UTILS}` exposures and narrow them to concrete utility roots where possible.
- [x] Update include forms only when target include-root changes require it.
- [x] Do not introduce `../` or `../../` include traversal.
- [x] Re-run header self-containment through the final checker after this phase.

Validation:

```bash
cmake --build build --target iEDA -- -j 16
```

Aggregation decisions:

- `icts_source` remains the source-layer package facade used by API/test/root executable-facing targets.
- `icts_source_database` remains the database category facade and is not linked from internal implementation targets.
- `icts_source_utils` remains the utility category facade and is not linked from internal implementation targets.
- `icts_source_module` remains the module category facade and no longer drags the full database aggregate.

## Phase 5: Root Aggregation And Debug Option Cleanup

Goal: keep aggregate targets useful at boundaries while preventing them from hiding implementation dependencies.

- [x] Review `icts_source`, `icts_source_database`, `icts_source_module`, and `icts_source_utils`.
- [x] Document which aggregate targets remain intentional boundary targets.
- [x] Remove aggregate dependencies from internal implementation targets when concrete targets are available.
- [x] Keep API/test/root executable-facing aggregation behavior stable.
- [x] Audit `DEBUG_ICTS_*` options used by touched CMake targets.
- [x] Add or normalize debug option declarations only if they are already used by touched targets and belong to this CMake cleanup.

Validation:

```bash
cmake --build build --target iEDA -- -j 16
```

## Phase 6: Full Build And Focused Tests

- [x] Verify no explicit CTS archive paths exist:

```bash
rg -n "CMAKE_ARCHIVE_OUTPUT_DIRECTORY|\\.a\\b|libicts_.*\\.a" src/operation/iCTS -g 'CMakeLists.txt' || true
```

- [x] Build `iEDA`:

```bash
cmake --build build --target iEDA -- -j 16
```

- [x] Build focused CTS/H-tree targets:

```bash
cmake --build build --target \
  icts_source_flow_synthesis_htree \
  icts_source_flow_synthesis_htree_solution \
  icts_source_flow_synthesis_htree_analytical_solver \
  icts_source_flow_synthesis_topology \
  icts_source_flow_synthesis_trace \
  icts_test_flow_synthesis_htree \
  icts_test_flow_synthesis_htree_analytical_solver -- -j 16
```

- [x] Run focused H-tree tests:

```bash
ctest --test-dir build -R '^icts_test_flow_synthesis_htree$|^icts_test_flow_synthesis_htree_analytical_solver$' --output-on-failure
```

## Phase 7: Real Design Validation

- [x] Run `ics55_dev` only after Phase 6 passes:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- [x] Capture and report:
  - [x] success/failure status
  - [x] runtime if available
  - [x] any CTS CMake/build-related warnings or runtime regressions

## Phase 8: Final ecc dev Check

- [x] Run final checker only after `ics55_dev` passes:

```bash
cd /home/liweiguo/project/ecc-tools-dev
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

- [x] Fix all in-scope findings.
- [x] Re-run the same full checker until in-scope findings are 0.
- [x] Record out-of-scope findings if they remain.

## Phase 9: Final Report, No Commit

- [x] Confirm final diff is uncommitted.
- [x] Summarize completed checklist phases and any intentionally deferred items.
- [x] Report validation commands and outcomes.
- [x] Do not commit until the user explicitly asks.

## Rollback Points

- After Phase 1: public header cleanup can be reverted independently if target visibility becomes worse.
- After Phase 2: synthesis CMake cleanup can be isolated from module/database aggregation cleanup.
- After Phase 3: aggregate-target replacement can be reverted target by target.
- Before Phase 7: do not run real design if build/tests are not clean.
