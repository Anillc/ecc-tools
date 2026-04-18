# iCTS Test Runtime Convergence

## Goal
Reduce the default iCTS test runtime and the default `ecc_dev_tools` check burden on test code without modifying the `ecc_dev_tools` scripts themselves.

The mainline for this phase is no longer feature expansion. The mainline is:
- remove redundant real-tech tests when the same behavior can be covered by unit tests
- keep only representative real-tech smoke coverage in the default lane
- move multi-build or policy-matrix real-tech coverage behind an opt-in slow regression switch
- keep the completed synthesis functionality intact while making the default verification path materially lighter

## Scope
- `src/operation/iCTS/test/CMakeLists.txt`
- `src/operation/iCTS/test/flow/htree/`
- `src/operation/iCTS/test/flow/synthesis/`
- `src/operation/iCTS/test/module/characterization/`
- `src/operation/iCTS/test/module/topology/linear_clustering/realtech/`

Out of scope for this phase:
- changing `ecc_dev_tools` scripts
- changing source-layer CTS behavior for the purpose of test reduction
- removing valuable real-tech coverage without preserving an opt-in regression path

## Mainline Strategy

### 1. Default lane keeps only smoke
- Add `ICTS_BUILD_SLOW_REALTECH_TESTS` with default `OFF`.
- When the option is `OFF`, the default real-tech executables only register representative smoke cases.
- When the option is `ON`, additional slow regression executables are built and registered.

### 2. Remove redundant real-tech coverage
- If a test only verifies caller-side bookkeeping or failure cleanup and does not require real-tech assets, move it to a unit test target.
- Do not keep a real-tech test if its only value is to re-check behavior already covered by a cheaper unit test.
- If one real-tech module mixes smoke coverage and sweep or policy-matrix regression in the same executable, keep only one representative smoke path in the default lane and move the rest behind the slow regression switch.

### 3. Avoid repeated heavy builds in the default lane
- Keep the end-to-end real-tech materialization smoke.
- Move policy toggles, frontier-family sweeps, and boundary fallback matrix checks out of the default lane.
- Keep slow coverage available behind the opt-in switch instead of deleting it outright when it still has diagnostic value.

## Default-vs-Slow Partition

### Default lane
- `icts_test_flow_htree_realtech`
  - `HTreeBuilderRealTechSmokeTest.SynthesizesMaterializedHTreeFromRealClockLoads`
- `icts_test_flow_synthesis_realtech`
  - `ClockSynthesisRealTechSmokeTest.ClusteredModeBuildsCentroidBuffersAndUsesUnrestrictedHtreeFrontier`
  - `ClockSynthesisRealTechSmokeTest.NonClusteredModeSkipsClusterBuffersAndUsesLeafUnbufferedHTree`
- `icts_test_module_characterization_realtech`
  - `CharacterizationRealTechSmokeTest.ManualHTreeCompositionProducesInspectableReport`

### Slow regression lane
- Enabled only when `ICTS_BUILD_SLOW_REALTECH_TESTS=ON`
- `icts_test_flow_htree_realtech_regression`
  - branch-buffer materialization coverage
  - caller-facing branch-buffer override coverage
  - caller-facing leaf-unbuffered coverage
  - caller-facing boundary fallback coverage
- `icts_test_module_characterization_realtech_regression`
  - terminal branch-buffer stability coverage
  - exact compose / exact join regression coverage
  - table-axis fallback and overflow reporting coverage
- `icts_test_module_topology_linear_clustering_realtech_regression`
  - diameter ladder responsiveness coverage
  - exact-cap sweep regression coverage
  - root-collision legalization coverage

### Removed from real-tech lane
- `ClockSynthesisRealTechSmokeTest.ClockFacadeClearsBorrowedInsertedObjectsBeforeFailingRebuild`
  - moved to `icts_test_flow_synthesis` unit coverage because it only verifies `Clock`-side stale inserted-object cleanup before a failing rebuild

### Linear-clustering default lane
- `icts_test_module_topology_linear_clustering_realtech`
  - `LinearClusteringRealTechSweepTest.RealTechOrFallbackSweepsGenerateArtifacts`

## Acceptance Criteria
- [ ] Default build keeps `ICTS_BUILD_SLOW_REALTECH_TESTS=OFF`.
- [ ] Default real-tech `gtest_list_tests` output only shows the smoke cases listed above.
- [ ] Slow regression targets are only added when `ICTS_BUILD_SLOW_REALTECH_TESTS=ON`.
- [ ] Rebuild-guard coverage exists in the unit target `icts_test_flow_synthesis`.
- [ ] Default smoke targets still pass locally after the lane split.
- [ ] Default runtime is materially lower than the previous baseline for `htree`, `synthesis`, and `characterization` real-tech targets.
- [ ] Default real-tech runtime for `linear_clustering` is reduced by keeping only one representative real-tech clustering smoke case.
- [ ] The implementation does not modify `ecc_dev_tools` scripts.

## Results

### Previous baseline
Measured earlier in this task on 2026-04-18:

| Target | Previous Runtime |
|--------|------------------|
| `icts_test_flow_htree_realtech` | `347.41s` |
| `icts_test_flow_synthesis_realtech` | `94.615s` |
| `icts_test_module_characterization_realtech` | `230s` |

### Current default-lane runtime
Measured on 2026-04-18 after the test/runtime convergence changes:

| Target | Current Runtime | Delta |
|--------|-----------------|-------|
| `icts_test_flow_htree_realtech` | `47.03s` | `-300.38s` |
| `icts_test_flow_synthesis_realtech` | `55.32s` | `-39.295s` |
| `icts_test_module_characterization_realtech` | `56.14s` | `-173.86s` |
| `icts_test_module_topology_linear_clustering_realtech` | `6.07s` | `-1.48s` |

### Current default real-tech case count
| Target | Previous Default Cases | Current Default Cases |
|--------|------------------------|-----------------------|
| `icts_test_flow_htree_realtech` | `5` | `1` |
| `icts_test_flow_synthesis_realtech` | `3` | `2` |
| `icts_test_module_characterization_realtech` | `6` | `1` |
| `icts_test_module_topology_linear_clustering_realtech` | `6` | `1` |

### Default executable runtime ranking
Measured on 2026-04-18 after the `linear_clustering` default-lane reduction. This ranking is the default `icts_test_*` burden relevant to routine local verification and `ecc_dev_tools`-visible test cost.

| Rank | Target | Cases | Runtime | Note |
|------|--------|-------|---------|------|
| `1` | `icts_test_flow_synthesis_realtech` | `2` | `55.153s` | still the heaviest default lane; dominated by end-to-end synthesis on real-tech loads |
| `2` | `icts_test_module_characterization_realtech` | `1` | `54.909s` | one smoke case remains, but characterization itself is intrinsically expensive |
| `3` | `icts_test_flow_htree_realtech` | `1` | `46.859s` | already reduced to one representative materialization smoke |
| `4` | `icts_test_module_topology_linear_clustering_realtech` | `1` | `6.065s` | reduced to a single representative clustering smoke |
| `5` | `icts_test_module_routing` | `11` | `1.327s` | cheap enough; not a runtime concern |
| `6` | `icts_test_module_topology_linear_clustering` | `16` | `0.121s` | synthetic/unit coverage is already cheap |
| `7` | `icts_test_module_topology_gen` | `5` | `0.058s` | cheap |
| `8` | `icts_test_flow_synthesis` | `5` | `0.018s` | cheap |
| `9` | `icts_test_flow_htree` | `3` | `0.014s` | cheap |
| `10` | `icts_test_module_characterization` | `15` | `0.009s` | cheap |
| `11` | `icts_test_database_spatial` | `4` | `0.004s` | cheap |
| `12` | `icts_test_common_io_artifact` | `2` | `0.003s` | cheap |
| `13` | `icts_test_common_logging` | `1` | `0.003s` | cheap |

### Runtime analysis and next-step optimization direction
- The default burden is now concentrated in three real-tech executables: `synthesis`, `characterization`, and `htree`.
- `linear_clustering` is no longer part of the dominant runtime bucket after shrinking the default lane from `6` real-tech cases to `1`.
- The next meaningful runtime win will not come from deleting more cheap unit targets; it will have to come from reducing real-tech characterization reuse cost inside `synthesis`, `characterization`, or `htree`, or from further shrinking representative load sizes.
- Optional regression targets remain opt-in under `ICTS_BUILD_SLOW_REALTECH_TESTS=ON` and are intentionally excluded from the default ranking above.

## Validation Notes
- `ICTS_BUILD_SLOW_REALTECH_TESTS:BOOL=OFF`
- `./bin/icts_test_flow_htree_realtech --gtest_list_tests`
  - only `SynthesizesMaterializedHTreeFromRealClockLoads`
- `./bin/icts_test_flow_synthesis_realtech --gtest_list_tests`
  - only clustered / non-clustered smoke
- `./bin/icts_test_module_characterization_realtech --gtest_list_tests`
  - only `ManualHTreeCompositionProducesInspectableReport`
- `./bin/icts_test_flow_synthesis --gtest_filter='ClockSynthesisTest.ClockFacadeClearsBorrowedInsertedObjectsBeforeFailingRebuild'`
  - passed in `1 ms`

## Follow-up
- Shrink test support compile surface further, especially characterization support and visualization support.
- Decide whether `module/topology/linear_clustering/realtech` also needs the same smoke-vs-regression split.
- After runtime convergence is accepted, run the final `.trellis/spec/` compliance review and full `ecc_dev_tools` acceptance pass.
