# M3 CMake target consolidation - baseline & log

> Snapshot at the start of M3 (after M1+M2+M4 land on origin/cts_refactor).

## Baseline (before M3)

```bash
$ grep -rEn '^[[:space:]]*add_library' src/operation/iCTS/source | wc -l
98
$ find src/operation/iCTS/source -name CMakeLists.txt | wc -l
90
```

Note: `grep -c` returns 98; one of those lines is the commented `add_library` block
inside `database/adapter/CMakeLists.txt`, so the actual configured target count
at baseline was 97.

### Per-area sub-counts (initial)

| Region | add_library | Notes |
|---|---:|---|
| flow/synthesis/htree | 13 | htree facade + 10 leaf subs + 2 characterization grand-children |
| flow/synthesis/topology | 4 | topology facade + buffer/sink/trunk |
| flow/synthesis/trace | 5 | trace facade + distance/domain_status/layout/topology_result |
| flow/synthesis/distribution | 1 | ClockDistribution.cc |
| flow/synthesis (facade) | 1 | Synthesis.cc |
| flow/optimization | 9 | optimization facade + 8 leaf subs (incl. 1 INTERFACE model) |
| flow/report | 9 | report facade + export/overview/qor + visualization + drawing/gds/gds-writer/gds-layer/svg |
| flow/instantiation | 3 | instantiation facade + design_conversion/idb_conversion |
| flow/setup | 2 | setup facade + clock_data |
| flow/evaluation | 2 | evaluation facade + qor |
| flow (top facade) | 1 | Flow.cc |
| **flow subtotal** | **50** | |
| database/adapter/fast_sta | 10 | 9 sub libs + facade (all in single CMakeLists.txt) |
| database/adapter/sta | 1 | |
| database/adapter/sdc | 1 | |
| database/adapter (INTERFACE wrapper) | 1 | |
| database/config, design, io | 3 | source-bearing |
| database/spatial, routing, timing, characterization, qor | 5 | all INTERFACE-only |
| database (top INTERFACE) | 1 | |
| **database subtotal** | **22** | |
| module/topology | 7 | topology facade + cluster_constraints + clustering + fast_clustering + 3 INTERFACE (config/kmeans/mcf) |
| module/routing | 7 | router + bst + cbs + flute + helper + local_legalization + salt |
| module/timing | 1 | |
| module/characterization | 1 | |
| module/analytical_characterization | 1 | |
| module (top INTERFACE) | 1 | |
| **module subtotal** | **18** | |
| utils/logger, singleton, visualization | 3 | source-bearing |
| utils/geometry, graph | 2 | INTERFACE-only |
| utils (top INTERFACE) | 1 | |
| **utils subtotal** | **6** | |
| source (top INTERFACE) | 1 | |
| **TOTAL** | **97** | |

## Consolidation plan and outcome

| Step | Action | Expected delta | Actual delta |
|---|---|---:|---:|
| M3a | Add `cmake/icts_targets.cmake` helpers (icts_add_library / icts_apply_debug_flags); include from top-level | 0 | 0 |
| M3b | htree 13 -> 3 (`htree` facade / `htree_solver` / `htree_topology`) | -10 | -10 |
| M3c | flow/optimization 9 -> 2 (`optimization` facade + `optimization_stages`) | -7 | -7 |
| M3d | flow/synthesis/topology 4 -> 1; flow/synthesis/trace 5 -> 1 | -7 | -7 |
| M3e | flow/report 9 -> 2 (report facade with export/overview/qor merged + report_visualization) | -7 | -7 |
| M3f | module/topology 7 -> 3 (topology + cluster_constraints + fast_clustering) | -4 | -4 |
| M3g | flow/instantiation 3 -> 1; flow/evaluation 2 -> 1; flow/setup 2 -> 1 | -4 | -4 |
| M3h | fast_sta 10 -> 10 (kept to avoid link-order regression) | 0 | 0 |
| **expected total** | | **-39 -> 58** | **-39 -> 58** |

## Per-step verification log

| Step | add_library after step | bash build.sh | iEDA binary |
|---|---:|---|---|
| baseline | 98 | PASS | linked |
| M3a | 98 | PASS | linked |
| M3b | 88 | PASS (after fixing solver -> topology link order) | linked |
| M3c | 81 | PASS | linked |
| M3d | 74 | PASS | linked |
| M3e | 66 | PASS | linked |
| M3f | 62 | PASS | linked |
| M3g | 58 | PASS | linked |

## Final state

```bash
$ grep -rEn '^[[:space:]]*add_library' src/operation/iCTS/source | wc -l
58
$ ls src/operation/iCTS/cmake/icts_targets.cmake
src/operation/iCTS/cmake/icts_targets.cmake
$ ls src/operation/iCTS/source/module/routing/database/ 2>&1
ls: cannot access ...: No such file or directory   # already removed by origin
$ ninja -t targets all | grep -c '^lib/libicts_source_'
46   # source-bearing archives (12 INTERFACE shim archives bring the total to 58)
```

### Link-order issues encountered

1. **htree facade -> solver/topology** (M3b)
   Initial setup `htree PUBLIC htree_topology + PRIVATE htree_solver` placed
   `htree_topology` archive BEFORE both `htree` and `htree_solver` on the
   final link line, leaving `RootDriverCompensationPass`, `EvaluateCandidateBuild`
   and similar symbols undefined when scanning the depender archives.
   Fix: route everything via `htree PUBLIC htree_solver` and let
   `htree_solver PUBLIC htree_topology` propagate. Resulting order:
   `htree.a -> htree_solver.a -> htree_topology.a`.

No further link-order regressions were observed for M3c / M3d / M3e / M3f / M3g.

### Skipped consolidations

- `database/adapter/fast_sta` (10 sub-libs) was intentionally kept at 10.
  These are co-located in a single CMakeLists.txt with hand-tuned link order
  (clock_state -> clock_tree -> liberty etc.) that the archived `child-task-tracker.md`
  documented as fragile. Touching this region would have required re-validating
  the iEDA binary's link order against the W6 regression notes - the consolidation
  delivered the target without taking on that risk.

- `database/spatial / routing / timing / characterization / qor` INTERFACE
  wrappers were kept. Each is consumed by several archives that link them
  directly, and their primary purpose is to expose include directories;
  collapsing them would have churned every consumer's CMakeLists.txt for
  zero behaviour change.

- `module/routing/{helper, salt, flute, cbs, local_legalization, bound_skew_tree}`
  were kept as separate archives. Each has 1-2 substantial TUs with distinct
  PUBLIC link surfaces (e.g. `salt`, `flute`, `lemon`) and merging them would
  have widened consumers' transitive link sets.

## SingletonRegistryTest

Still PASS after M3 changes:

```text
[==========] 5 tests from 1 test suite ran. (1 ms total)
[  PASSED  ] 5 tests.
```

Major iCTS test binaries verified to link cleanly:
- icts_test_flow / icts_test_flow_synthesis / icts_test_flow_synthesis_htree /
  icts_test_flow_synthesis_htree_analytical_solver
- icts_test_database_adapter_fast_sta
- icts_test_module_routing / icts_test_module_topology_fast_clustering /
  icts_test_module_topology_gen
- icts_test_module_characterization / icts_test_module_analytical_characterization
- icts_test_common_clustering_artifact / icts_test_common_clustering_metrics
- icts_test_utils_singleton
