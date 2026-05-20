# CTS semantic source naming cleanup

## Goal

Replace source names that describe implementation mechanics with CTS business/domain names that identify the object and owned action.

## Parent Task

`.trellis/tasks/05-19-cts-code-normalization-refactor-research`

## Scope

Source naming only. Test helper naming is handled later by `.trellis/tasks/05-19-cts-test-helper-semantic-cleanup`.

Initial source examples:

- `WrapperClockWriterInternal.hh`
- `WrapperClockWriterSupport.cc`
- `STAAdapterInternal.hh/.cc`
- `QorEvaluationInternal.hh`
- `AnalyticalSolverRequest.cc`
- `OptimizationTypes.hh`
- `BSTRouterInternal.hh`
- `FastClusteringInternal.hh`

## Requirements

- Forbid vague engineering names in CTS source unless the user explicitly approves a specific exception.
- Do not use vague replacements such as `rollback`, `fallback`, `Input`, `Session`, or `Network`.
- Avoid `Network` especially because it conflicts with CTS `Net` / clock-net semantics.
- Prefer CTS object/action names such as clock-net membership, inserted-clock object, route tree, RC tree, Liberty arc table, sink-domain build, and
  clock-sizing edit/solve.
- If a concrete CTS name cannot be determined, list the unresolved name and ask the user to define the naming standard before editing.
- Rename one domain at a time.

## Implementation Checklist

- [x] Build a rename table for each source domain before editing.
- [x] Confirm every proposed new name states a CTS object and owned action.
- [x] Confirm no unresolved source names require user naming input before editing.
- [x] Rename source files/types/includes one domain at a time.
- [x] Update CMake with each rename.
- [x] Keep behavior unchanged.
- [x] Leave test helper naming for the final child task, except source-facing test references required to compile.

## Acceptance Criteria

- [x] No touched source file/type uses vague engineering naming without explicit user-approved exception.
- [x] New names are traceable to CTS objects/actions.
- [x] Include and CMake references are updated.
- [x] Affected targets build.

## Validation

```bash
ninja -C build <affected-targets>
ctest --test-dir build -N -R icts
```

## Validation Evidence

- Built all affected source/test targets:
  `icts_source_database_io`, `icts_source_flow_instantiation_idb_conversion`, `icts_source_database_design`,
  `icts_source_flow_synthesis_trace_layout`, `icts_source_flow_synthesis_distribution`, `icts_source_flow_synthesis_topology`,
  `icts_source_flow_synthesis_htree_compensation`, `icts_source_module_topology_cluster_constraints`, `icts_source_flow_evaluation_qor`,
  `icts_source_database_adapter_sta`, `icts_source_database_adapter_sdc`, `icts_source_database_adapter_fast_sta`,
  `icts_source_module_routing_bst`, `icts_source_module_routing`, `icts_source_module_topology_fast_clustering`,
  `icts_source_flow_synthesis_htree_analytical_solver`, `icts_test_flow_synthesis_htree_analytical_solver`,
  `icts_source_flow_optimization_clock_sizing_edit`, `icts_source_flow_optimization`, `icts_source_flow_optimization_state`,
  `icts_source_flow_optimization_candidate`, `icts_source_flow_optimization_preparation`, `icts_source_flow_optimization_solver`,
  `icts_source_flow_optimization_report`, `icts_source_flow`, and `icts_test_database_adapter_fast_sta`.
- Executed affected tests:
  `ctest --test-dir build -R '^(icts_test_database_adapter_fast_sta|icts_test_flow_synthesis_htree_analytical_solver)$' --output-on-failure`.
- Enumerated iCTS test registration:
  `ctest --test-dir build -N -R icts`.
- Source filename scan for forbidden generic names returned no matches.
- Legacy source-name scan returned no matches for the renamed files/types.
- `git diff --check` passed.
