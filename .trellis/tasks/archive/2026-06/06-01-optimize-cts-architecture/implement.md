# Implementation Plan

## Phase 0: Planning Gate

- [x] Confirm the `use_netlist` / `net_list` compatibility decision:
      clean up/deprecate and do not implement net-list based CTS input.
- [x] Review and accept `prd.md` and `design.md`.
- [x] Start the task with `python3 ./.trellis/scripts/task.py start
      06-01-optimize-cts-architecture` only after the planning gate is accepted.

## Phase 1: Status and Entry Contract

- [x] Add a lightweight CTS status/result type at the API boundary.
- [x] Change `CTSAPI::init`, `CTSAPI::runCTS`, and `CTSAPI::report` to return
      status.
- [x] Add or update internal flow result/summary types so `CTSAPI` does not have
      to infer success from side effects.
- [x] Update `CtsIO` to propagate status instead of returning hard-coded
      success.
- [x] Update tool-manager, Tcl, and Python CTS entry points to check and surface
      status.
- [x] Add focused tests for successful status propagation and at least one
      expected failure path.
- [x] Build and run focused API/tool-manager/Tcl/Python compile checks.

## Phase 2: Config Contract

- [x] Remove/deprecate `use_netlist` and `net_list` across Tcl config, sample
      JSON, runtime parsing, and docs/comments.
- [x] Add a consistent warning for unknown config keys: invalid config key.
- [x] Add a consistent warning for deprecated config keys: this item is no
      longer used.
- [x] Ensure deprecated/unknown key warnings do not fail CTS by themselves.
- [x] Make invalid present numeric config values produce diagnostics.
- [x] Keep documented defaults for absent optional config values.
- [x] Add config parsing tests for valid values, missing defaults, unknown keys,
      deprecated keys, and invalid present values.
- [x] Verify setup failures propagate through the status contract added in
      Phase 1.

## Phase 3: CMake and Include Boundary

- [x] Remove or update stale `cmake/operation/icts.cmake` contents.
- [x] Audit iCTS `PUBLIC` and `INTERFACE` include roots.
- [x] Tighten one production target group at a time, starting with the broadest
      include exposures.
- [x] Keep test fixture convenience separate from production dependencies.
- [x] Verify no source-layer dependency on API is introduced.
- [x] Run build checks after each target group change.
- [x] If target wiring changes materially, repeat the duplicate-archive
      sensitivity check used by the prior CTS minimal compile-deps task.

## Phase 4: Facade Cleanup

- [x] Reassess `Flow`, `Synthesis`, and `Topology` after Phases 1-3.
- [x] Extract status/summary or helper units only where the boundary is stable
      and the name is domain-specific.
- [x] Preserve one readable public facade per behavior area.
- [x] Avoid generic names such as `stage`, `session`, or `context` unless the
      type has a narrow local meaning.
- [x] Keep algorithm behavior unchanged.

## Phase 5: Final Verification

- [x] `cmake --build build --target icts_test_database_config icts_test_flow iEDA -j 32`.
- [x] Focused iCTS `ctest` targets pass.
- [x] Representative runtime CTS flow passes with `ics55_dev` using
      `cd scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`.
- [x] `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`
      reports zero in-scope findings or documented follow-up decisions.
- [x] `git diff --check`.
- [x] Update task docs with actual implementation decisions.

## Implementation Summary

- Added `CTSStatus`/`CTSStatusCode` as the public CTS API status contract and moved it to `api/CTSStatus.hh` so `CTSAPI.hh` remains focused on the API facade.
- Added `FlowRunStatus`/`FlowReportStatus` at the flow boundary.
- Propagated init/run/report failures through API, tool-manager, Tcl, and Python CTS entry points.
- Removed active `use_netlist` / `net_list` config surfaces and left deprecated-key warnings for legacy user JSON.
- Added invalid-key warnings for unsupported config keys and deprecated-item warnings for removed keys.
- Changed invalid present numeric/routing-layer config values to fail parse with diagnostics, while missing optional keys still use defaults.
- Replaced stale legacy iCTS CMake include metadata and tightened the config target include boundary.
- Added focused config parser tests and API/flow status tests.

## Verification Results

- `cmake --build build --target icts_test_database_config icts_test_flow iEDA -j 32`: passed.
- `ctest --test-dir build -R '^(icts_test_database_config|icts_test_flow)$' --output-on-failure`: passed.
- `git diff --check`: passed.
- `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`: passed; CTS flow and report finished.
- `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`: passed with 0 in-scope findings.

## Follow-up Status Placement Cleanup

- Moved `CTSStatusCode` and `CTSStatus` out of `CTSAPI.hh` into `api/CTSStatus.hh`.
- Kept `CTSAPI` signatures and behavior unchanged.
- Added an explicit `CTSStatus.hh` include in `FlowTest.cc` for direct status-code assertions.

### Follow-up Verification

- `cmake --build build --target icts_test_database_config icts_test_flow iEDA -j 32`: passed.
- `ctest --test-dir build -R '^(icts_test_database_config|icts_test_flow)$' --output-on-failure`: passed.
- `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`: passed; CTS flow and report finished.
- `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`: passed with 0 in-scope findings.
