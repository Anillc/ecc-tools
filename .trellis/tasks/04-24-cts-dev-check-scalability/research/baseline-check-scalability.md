# iCTS Check Scalability Baseline

Date: 2026-04-24

## Scope

This baseline captures the iCTS translation units and CMake dependency paths that make the full `ecc_dev_tools` check hard to complete. The intended follow-up is behavior-preserving refactoring only: split cohesive CTS responsibilities into smaller files and narrow test dependencies where the current graph is broader than the test needs.

## Confirmed Checker Blockers

The latest no-timeout full iCTS check was stopped manually after roughly two hours. The remaining CPU-bound `clang-tidy` processes were:

| Translation unit | Role | Refactor priority |
| --- | --- | --- |
| `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc` | Native H-tree construction and materialization | P0 |
| `src/operation/iCTS/source/flow/numerical_htree/NumericalHTreeBuilder.cc` | Numerical H-tree model/search/compare flow | P0 |
| `src/operation/iCTS/source/module/characterization/CharBuilder.cc` | iSTA/iPA characterization sampling | P0 |
| `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc` | Real-tech synthesis smoke regression | P1 |

The 600s full-check run had `0` in-scope findings before timing out on the same area, so the immediate problem is checker scalability over large translation units, not known findings.

## Largest iCTS Files

Command:

```bash
rg --files src/operation/iCTS | rg '\.(cc|hh)$' | xargs wc -l | sort -nr | sed -n '1,80p'
```

Top candidates:

| Lines | File | Initial decision |
| ---: | --- | --- |
| 2658 | `source/flow/htree/HTreeBuilder.cc` | Split in native H-tree phase |
| 2296 | `source/module/routing/bound_skew_tree/BoundSkewTree.cc` | Triage after P0 blockers; split if still checker-heavy |
| 1840 | `test/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentScenario.cc` | Triage after P0/P1 blockers; likely scenario support split |
| 1571 | `source/database/adapter/sta/STAAdapter.cc` | Triage carefully because it sits on the iSTA boundary |
| 1178 | `source/module/characterization/CharBuilder.cc` | Split in characterization phase |
| 1003 | `test/flow/htree/HTreeVisualizationSupport.cc` | Split with test/support phase if still above threshold |
| 998 | `source/flow/numerical_htree/NumericalHTreeBuilder.cc` | Split first |
| 890 | `source/module/routing/bound_skew_tree/GeomCalc.cc` | Triage with BoundSkewTree |
| 821 | `test/flow/htree/HTreeBuilderRealTechSmokeTest.cc` | Split with H-tree real-tech test support if needed |
| 811 | `test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc` | Split in real-tech smoke phase |
| 802 | `test/module/characterization/support/CharacterizationRealTechTestSupport.cc` | Triage with characterization real-tech support |

Working threshold: files above about 800 lines are treated as overlarge and must be either split or explicitly deferred with a reason.

## Heavy Target Pattern

`src/operation/iCTS/test/CMakeLists.txt` currently defines:

```text
icts_test_base -> log + icts_source + icts_api + icts_test_external_libs
icts_test_realtech_base -> icts_test_base + idm
icts_add_test_executable(...) -> icts_test_base + icts_test_common_io + requested libs
```

`icts_source` then fans into broad source aggregators:

```text
icts_source -> icts_source_database + icts_source_flow + icts_source_module + icts_source_utils
```

This makes narrow test binaries inherit large compile/check dependency contexts even when they only exercise one module.

Examples from the current CMake file-api reply:

| Target | Sources | Dependencies |
| --- | ---: | ---: |
| `icts_test_module_topology_linear_clustering_realtech_regression` | 5 | 149 |
| `icts_test_module_topology_linear_clustering` | 6 | 148 |
| `icts_test_module_topology_linear_clustering_realtech` | 2 | 145 |
| `icts_test_flow_htree_realtech` | 3 | 142 |
| `icts_test_flow_numerical_htree_arm9_comparison` | 3 | 139 |
| `icts_test_flow_synthesis_realtech` | 3 | 139 |
| `icts_test_flow_htree` | 2 | 132 |
| `icts_test_flow_numerical_htree` | 2 | 131 |
| `icts_test_module_characterization` | 5 | 131 |
| `icts_test_module_numerical_characterization` | 2 | 131 |

## Refactor Guard Rails

* Preserve algorithms, selected patterns, logs, reports, default test skip behavior, and QoR semantics.
* Prefer moving cohesive existing code into domain-named files over rewriting logic.
* Keep public headers stable unless a small internal header is needed to share moved helpers.
* Avoid generic service/facade/manager layers. Use CTS names such as level planning, segment frontier, actual-load legality, materialization, characterization sampling, or real-tech setup.
* Keep adapter-boundary code in the adapter layer; do not move iSTA/iDB details into algorithm modules.
* Add new `.hh/.cc` files with iCTS copyright and Doxygen headers.

## Validation Plan

Focused validation before final full check:

| Refactor area | Focused targets |
| --- | --- |
| Numerical H-tree | `icts_test_flow_numerical_htree`, `icts_test_flow_numerical_htree_arm9_comparison` |
| Native H-tree | `icts_test_flow_htree`; real-tech H-tree targets when assets are available |
| Characterization | `icts_test_module_characterization`, `icts_test_module_numerical_characterization` |
| Clock synthesis real-tech tests | `icts_test_flow_synthesis`, `icts_test_flow_synthesis_realtech` default skip path |
| Target graph narrowing | impacted test executables and CMake configure/build |

Final validation remains:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

If the final full check still does not converge, record remaining TUs, elapsed time, and process state instead of solving it by timeout-only policy.

## Refactor Result Snapshot

Date: 2026-04-24

The first decomposition pass has split every initially overlarge iCTS file above the working threshold of roughly 800 lines. The largest remaining iCTS `.cc/.hh` file is now below the threshold:

| Lines after split | File | Notes |
| ---: | --- | --- |
| 778 | `source/database/adapter/sta/STAAdapterInternal.cc` | Internal STA/iSTA/iPA helper implementation shared by smaller adapter TUs |
| 754 | `source/module/topology/linear_clustering/LinearOrderGenerator.cc` | Below threshold, not part of confirmed checker blockers |
| 738 | `test/module/topology/linear_clustering/synthetic/support/reference/LinearClusteringSyntheticReference.cc` | Below threshold |
| 692 | `test/flow/synthesis/ClockSynthesisVisualizationSupport.cc` | Below threshold |
| 686 | `source/module/routing/bound_skew_tree/BSTRouter.cc` | Below threshold |

Completed source/test decompositions:

| Original file | Main split files |
| --- | --- |
| `source/flow/numerical_htree/NumericalHTreeBuilder.cc` | response surface, model adapter, search, topology adapter, comparison |
| `source/flow/htree/HTreeBuilder.cc` | build options, level plan, segment frontier, actual-load checks, composition, materialization, logging |
| `source/module/characterization/CharBuilder.cc` | config, build orchestration, topology conversion, pattern enumeration, sampling, char circuit wiring |
| `source/module/routing/bound_skew_tree/BoundSkewTree.cc` | flow, topology, joining, balance, embedding |
| `source/module/routing/bound_skew_tree/GeomCalc.cc` | line geometry, point/region geometry, transformed-rect geometry |
| `source/database/adapter/sta/STAAdapter.cc` | init/reset, cell query, clock lookup, timing update, char circuit, char timing, char power, wire RC, shared internal helpers |
| `test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc` | smoke scenarios plus shared real-tech smoke support |
| `test/flow/htree/HTreeBuilderRealTechSmokeTest.cc` and `HTreeVisualizationSupport.cc` | smoke support, visualization rendering, delay-power/report rendering |
| `test/module/characterization/support/CharacterizationRealTechTestSupport.cc` | base real-tech char support plus exact-frontier support |
| `test/module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentScenario.cc` | scenario orchestration, input/strategy helpers, synthetic input generation, reporting |

Focused validation completed after the splits:

```bash
cmake --build build --target \
  icts_test_flow_htree \
  icts_test_flow_numerical_htree \
  icts_test_module_characterization \
  icts_test_module_numerical_characterization \
  icts_test_module_routing \
  icts_test_flow_htree_realtech \
  icts_test_flow_synthesis_realtech \
  icts_test_module_topology_linear_clustering_realtech_experiment_scenario_support \
  -j 8
```

Result: passed.

Runtime validation completed:

```bash
./bin/icts_test_flow_htree
./bin/icts_test_flow_numerical_htree
./bin/icts_test_module_characterization
./bin/icts_test_module_numerical_characterization
./bin/icts_test_module_routing
./bin/icts_test_flow_htree_realtech --gtest_filter='HTreeBuilderRealTechSmokeTest.Arm9FullSinkExperimentMatrix:HTreeBuilderRealTechSmokeTest.Arm9FullSinkExperimentMatrixAutoWireLengthUnit' --gtest_color=no
```

Result: all non-realtech tests passed; the two H-tree ARM9 matrix tests used the intended default skip path. Realtech smoke binaries were built and their test lists were checked. Full realtech smoke runtime was not rerun because those tests can execute full-design ARM9 flows and are intentionally long.

## Final Check Result

Date: 2026-04-24

The default full iCTS check now completes without a timeout or manual stop:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Result:

| Check kind | In-scope findings | Notes |
| --- | ---: | --- |
| `format` | 0 | 280 C/C++ files checked |
| `tidy` | 0 | 178 compile commands, deep mode, complete pass plan |
| `headers` | 0 | 108 scope-local headers self-checked |
| `cmake` | 0 | 80 targets analyzed |
| `iwyu` | 0 | 178 translation units analyzed; all remaining IWYU findings suppressed by existing whitelist rules |

Overall:

* Total runtime: `512.124s`.
* In-scope findings: `0`.
* Out-of-scope findings: `3469`, all in external `src/database/...` headers triggered by iCTS translation units.
* Original blocker TUs no longer blocked the run:
  * `source/flow/htree/HTreeBuilder.cc`
  * `source/flow/numerical_htree/NumericalHTreeBuilder.cc`
  * `source/module/characterization/CharBuilder.cc`
  * `test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc`

Additional focused validation after include cleanup:

```bash
cmake --build build --target \
  icts_test_flow_htree \
  icts_test_flow_numerical_htree \
  icts_test_module_characterization \
  icts_test_module_numerical_characterization \
  icts_test_module_routing \
  icts_test_flow_htree_realtech \
  icts_test_flow_synthesis_realtech \
  icts_test_flow_htree_realtech_regression \
  icts_test_module_topology_linear_clustering_realtech_experiment_scenario_support \
  -j 8
```

Result: passed.

Runtime/list validation after include cleanup:

```bash
./bin/icts_test_flow_htree
./bin/icts_test_module_characterization
./bin/icts_test_module_routing
./bin/icts_test_flow_numerical_htree
./bin/icts_test_module_numerical_characterization
./bin/icts_test_flow_htree_realtech --gtest_list_tests
./bin/icts_test_flow_synthesis_realtech --gtest_list_tests
```

Result: passed for lightweight tests and registration/list paths.
