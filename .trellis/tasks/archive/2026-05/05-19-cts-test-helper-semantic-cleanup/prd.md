# CTS test helper semantic cleanup

## Goal

Clean CTS test helper names and organization after source boundaries and source naming are stable.

## Parent Task

`.trellis/tasks/05-19-cts-code-normalization-refactor-research`

## Scope

- `src/operation/iCTS/test/common`
- `src/operation/iCTS/test/flow`
- `src/operation/iCTS/test/flow/synthesis`
- `src/operation/iCTS/test/module/characterization`
- `src/operation/iCTS/test/module/topology`
- Other CTS test helpers touched by source refactors.

## Requirements

- Source cleanup has priority over test helper cleanup.
- Rename tests after related source boundaries and names are stable.
- Prefer helper names that describe test role, such as `Fixture`, `CaseBuilder`, `Scenario`, `ArtifactWriter`, `Golden`, and `RealTechAsset`.
- Keep real-tech/manual tests opt-in when assets are required.
- Keep default tests fast and deterministic.

## Implementation Checklist

- [x] Inventory remaining generic test helper folders/files after source cleanup.
- [x] Group test helpers by source domain and test role.
- [x] Rename helpers in domain slices.
- [x] Update includes, CMake, and test references.
- [x] Preserve test behavior and fixture data.

## Acceptance Criteria

- [x] Touched test helpers use role/domain names rather than generic `Support` / `Internal` naming.
- [x] Test layout mirrors source semantics where practical.
- [x] Default CTS tests remain fast and deterministic.
- [x] Affected tests pass.

## Validation

```bash
ninja -C build icts_test_flow icts_test_flow_synthesis icts_test_module_characterization
ninja -C build icts_test_flow icts_test_flow_synthesis icts_test_flow_synthesis_htree icts_test_module_characterization icts_test_common_logging icts_test_common_io_artifact icts_test_flow_synthesis_htree_analytical_solver icts_test_module_topology_gen
ctest --test-dir build --output-on-failure -R 'icts_test_(common_io_artifact|common_logging|flow|flow_synthesis|flow_synthesis_htree|flow_synthesis_htree_analytical_solver|module_characterization|module_topology_gen)$'
ninja -C build icts_test_database_adapter_fast_sta icts_test_database_design icts_test_database_spatial icts_test_module_analytical_characterization icts_test_module_routing icts_test_utils_graph
ninja -C build icts_test_module_topology_fast_clustering
ctest --test-dir build --output-on-failure -R '^icts_test_'
find src/operation/iCTS/test \( -path '*Support*' -o -path '*Internal*' -o -path '*Types*' -o -path '*Session*' -o -path '*Fallback*' -o -path '*Rollback*' -o -path '*Request*' -o -path '*Response*' -o -path '*Snapshot*' -o -path '*Network*' \) -print | sort
rg -n "ClusterArtifactSupport|ClusterGeometrySupport|RealTechSetupSupport|common/types/TestDataTypes|FlowTestSupport|TopologyRealTechMatrixSupport|TopologyRealTechSelectionSupport|TopologyRealTechSmokeSupport|TopologyVisualizationInternal|TopologyVisualizationSupport|HTreeRealTechMatrixSupport|HTreeRealTechSmokeSupport|HTreeVisualizationInternal|HTreeVisualizationSupport|CharacterizationRealTechComposeSupport|CharacterizationRealTechExactRegressionSupport|CharacterizationRealTechFallbackTest|CharacterizationRealTechFitSupport|CharacterizationRealTechFunctionGapSupport|CharacterizationRealTechFunctionModelSupport|CharacterizationRealTechTestSupport|CharacterizationTestSupport|FastClusteringRealTechBenchmarkInternal|TopologyGenInternal|TopologyGenArtifactSupport|TopologyGenCaseSupport|TopologyGenScenarioSupport" src/operation/iCTS/test src/operation/iCTS/source -g '*.cc' -g '*.hh' -g 'CMakeLists.txt'
git diff --check -- src/operation/iCTS/test src/operation/iCTS/source/database/adapter/fast_sta/CMakeLists.txt src/operation/iCTS/test/README.md .trellis/tasks/05-19-cts-test-helper-semantic-cleanup .trellis/tasks/05-19-cts-code-normalization-refactor-research/prd.md
```

Result:

- Core and additional CTS test targets built.
- Full built CTS test set passed: `15/15`.
- Generic test-helper path scan produced no matches.
- Old helper include/name scan produced no matches.
- Whitespace check passed.
- `icts_test_module_topology_fast_clustering` needed target-local `LINK_GROUP:RESCAN` because the topology facade test pulls iSTA/iPower static archives with mutual references.
