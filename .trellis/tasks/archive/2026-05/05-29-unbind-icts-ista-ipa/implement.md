# Implementation Plan

## Ordered Checklist

1. Baseline inventory
   - Re-run `rg` for `STAAdapter`, `api/TimingEngine.hh`, `api/TimingIDBAdapter.hh`, `api/Power.hh`, `ista-engine`, `power`, `ipower`, and `namespace ista` inside `src/operation/iCTS`.
   - Record the current target graph for iCTS CMake dependencies.

2. Wrapper RC migration
   - Add Wrapper RC query methods using iDB routing-layer data.
   - Move `queryConfiguredClockRouteSegmentRc` logic from `STAAdapter` to `Wrapper`.
   - Replace RC users in topology, H-tree compensation, characterization, FastSTA parasitics, FastSTA char, and QoR local RC metrics.
   - Add unit/golden checks for RC formula and unit normalization.

3. Wrapper Liberty migration
   - Add Wrapper Liberty lookup/cache methods for CTS-required cell data.
   - Change `FastStaLiberty::extractBufferCell` to consume `Wrapper`.
   - Replace buffer-port, pin-cap, slew-limit, cap-limit, cell-area/height, and classification users.
   - Move root-driver direct cost onto Wrapper/FastSTA-local Liberty helpers and remove iPA average/toggle helper usage.

4. FastSTA environment cleanup
   - Replace `STAAdapter*` in `FastStaEnvironment`, `FastStaCharTopologySpec`, and `FastStaClockContext`.
   - Update `FastStaBuilder`, `FastStaParasitics`, `FastStaChar`, optimization preparation, and char library construction.
   - Keep timing query behavior in `FastSTA`; do not create a separate TimingProvider class.

5. Full-design timing removal/replacement
   - Remove `refreshFullDesignTimingContext`, `setPropagatedClocks`, full `updateTiming`, and full `reportTiming` from CTS evaluation.
   - Remove setup/hold TNS/WNS/suggested-frequency report fields and table emission.
   - Remove full launch/capture latency/skew path metrics and table emission.
   - Remove root/leaf arrival probe report and related diagnostics unless rebuilt as pure FastSTA in a later task.
   - Remove iSTA RC-tree installation from final QoR.
   - Remove "unavailable full STA" warning logs because unsupported features are being deleted.

6. SDC/clock trace iSTA removal
   - Replace `WrapperClockReader` and `adapter/sdc/clock_trace` direct `ista::TimingEngine` Liberty lookups with Wrapper Liberty classification.
   - Keep side-effect-free SDC clock parsing intact.

7. Delete or detach STA adapter
   - Remove `database/adapter/sta` from iCTS source CMake.
   - Remove `STAAdapter` from `CTSRuntime`, flow setup, synthesis, instantiation, optimization, and evaluation inputs.
   - Delete obsolete adapter source only after all consumers are gone, or leave it unreferenced if deletion is too risky for the branch policy.

8. CMake cleanup
   - Remove `ista-engine` from iCTS external link lists.
   - Remove `power` from iCTS adapter link lists.
   - Update tests to depend on Wrapper/FastSTA fixtures instead of STA adapter fixtures.

9. CTS API feature summary cleanup
   - Remove `feature_ista.h` from `CTSAPI.cc`.
   - Stop mapping `QorSummary::clocks_timing` into `ieda_feature::CTSSummary`.
   - If safe, remove the CTS clock timing fields from the feature struct definition; otherwise leave the shared struct untouched but empty from CTS.

10. Validation
   - Build iCTS targets.
   - Run focused CTS tests for Wrapper, FastSTA, characterization, synthesis, optimization, and QoR reporting.
   - Run `rg` acceptance checks for iSTA/iPA headers and CMake links.
   - Compare representative RC/Liberty-derived values against the old path before deleting old artifacts.
   - Run the CTS binary on the `ics55_dev` design.
   - Run final `ecc dev` project check.

## Risky Files

- `src/operation/iCTS/source/flow/Flow.hh`
- `src/operation/iCTS/source/flow/setup/Setup.cc`
- `src/operation/iCTS/source/database/io/Wrapper.hh`
- `src/operation/iCTS/source/database/io/Wrapper.cc`
- `src/operation/iCTS/source/database/io/WrapperClockReader.cc`
- `src/operation/iCTS/source/database/adapter/sdc/clock_trace/*`
- `src/operation/iCTS/source/database/adapter/fast_sta/*`
- `src/operation/iCTS/source/flow/evaluation/qor/*`
- `src/operation/iCTS/source/flow/synthesis/**`
- `src/operation/iCTS/source/flow/optimization/**`
- `src/operation/iCTS/source/flow/instantiation/**`
- `src/operation/iCTS/source/**/CMakeLists.txt`

## Validation Commands

```bash
rg "api/TimingEngine.hh|api/TimingIDBAdapter.hh|api/Power.hh|ipower|STAAdapter" src/operation/iCTS/source
rg "ista-engine|\\bpower\\b|icts_source_database_adapter_sta" src/operation/iCTS
cmake --build <build-dir> --target <icts-target-or-test-target>
ctest --test-dir <build-dir> -R "iCTS|FastSTA|CTS"
```

The concrete build directory and test target names should be selected from the local build configuration before implementation starts.

## Open Decision Before Implementation

Resolved by user: delete any unsupported full-design timing functionality directly. Do not keep unavailable placeholder logs, report rows, CTS API summary mappings, or build-time optional iSTA integration inside iCTS.

## Completion Validation

- `cmake --build build --target icts_api tool_api_icts icts_test_flow icts_test_database_design icts_test_database_adapter_fast_sta -j 8` passed.
- `ctest --test-dir build -R "icts_test_" --output-on-failure` passed: 14/14 tests.
- iSTA/iPA production usage scans passed with no matches for removed headers, `STAAdapter`, iSTA/iPA runtime entry points, deleted full-design timing APIs, or iCTS `ista-engine`/`power` CMake links.
- `cmake --build build --target iEDA ecc_bin -j 8` passed.
- `scripts/design/ics55_dev/iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` passed with exit code 0; final run log files:
  - `scripts/design/ics55_dev/result_unbind_ista_ipa_final_20260529_105802.stdout`
  - `scripts/design/ics55_dev/result_unbind_ista_ipa_final_20260529_105802.stderr`
- `scripts/design/ics55_dev/result/metric/iCTS_metrics.json` contains only CTS-local summary fields:
  - `buffer_num`
  - `buffer_area`
  - `clock_path_min_buffer`
  - `clock_path_max_buffer`
  - `max_level_of_clock_tree`
  - `max_clock_wirelength`
  - `total_clock_wirelength`
- Run-output scans passed with no removed full-design timing fields or iSTA/iPA strings in the current CTS outputs.
- `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` passed with 0 in-scope findings.
