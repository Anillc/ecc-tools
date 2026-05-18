# Implementation Plan: CTS Code Structure Optimization

## Pre-Start Gate

- [x] User confirms fallback policy.
- [x] User confirms whether `flow/optimization` is a first-class CTS stage.
- [x] User confirms optimization knobs should not become user/global config; algorithm-related values should be consolidated into optimizer-owned `Options`.
- [x] User confirms test-scope policy: default/CI keeps fast deterministic tests; real-tech smoke/regression and benchmark suites move to explicit opt-in/manual targets.
- [x] Update planning artifacts after each answer.
- [x] Run `task.py start` only after planning is approved.

## Phase 0: Mechanical Compliance and Visibility

- [x] Fix license wording in iCTS `.cc`/`.hh` files:
  - from `MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.`
  - to `MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.`
- [x] Add required author line to:
  - `src/operation/iCTS/test/common/realtech/RealTechSetupSupport.hh`
  - `src/operation/iCTS/test/common/types/TestDataTypes.cc`
  - `src/operation/iCTS/test/module/topology/topology_gen/TopologyGenShared.hh`
- [x] Repair `ctest` registration so iCTS test executables are discoverable by `ctest -N -R icts`.
- [x] Remove or explain empty module placeholders:
  - `source/module/buffer_sizing/`
  - `source/module/buffering/CMakeLists.txt`
  - `source/module/drv/CMakeLists.txt`
  - `source/module/report/CMakeLists.txt`
- [x] Validate:
  - `ctest -N -R icts`
  - representative fast test build/run.

## Phase 1: Spec Alignment

- [x] Update backend directory spec to codify confirmed architecture:
  - add `source/flow/optimization/`;
  - define optimization responsibilities and boundaries;
  - update flow chain to include optimization.
- [x] Add any durable fallback/config principle to the relevant backend spec only if it is a global convention.

## Phase 2: Optimization.cc Behavior-Preserving Split

- [x] Add new optimization internal files and update `source/flow/optimization/CMakeLists.txt`.
- [x] Move data structs into `OptimizationTypes.hh`.
- [x] Move reporting code into `OptimizationReport.cc`.
- [x] Move committed mutation code into `OptimizationMutation.cc`.
- [x] Move preparation code into `OptimizationPreparation.cc`.
- [x] Move candidate generation and topology/window scoring into `OptimizationCandidates.cc`.
- [x] Move solver loops and trial application into `OptimizationSolver.cc`.
- [x] Keep `Optimization::run` as orchestration only.
- [x] Validate after each split:
  - `ninja -C build icts_source_flow_optimization`
  - `ninja -C build icts_test_flow icts_test_database_adapter_fast_sta`
  - selected test executables.

## Phase 3: Optimization Options and constexpr Audit

- [x] Introduce explicit `OptimizationOptions` or equivalent CTS-named policy type.
- [x] Move search budgets/thresholds out of file-local constexpr where they are algorithm knobs.
- [x] Keep optimization search knobs out of user/global `Config` for this task.
- [x] Centralize optimizer algorithm parameters in `OptimizationOptions` with documented defaults and validation.
- [x] Keep local constants only for true invariants, with comments for non-obvious numerical contracts.
- [x] Rename misleading local `fallback_id` in optimization if behavior remains.
- [x] Add tests for option resolution/default/failure behavior.

## Phase 4: Fallback Cleanup

- [x] Convert `FastStaChar` DBU fallback to explicit failure or explicit option.
- [x] Convert routing layer default 1 to explicit project default or config error.
- [x] Convert `CharBuilder` auto-derived `wirelength_unit_um` to explicit config or explicit opt-in auto mode.
- [x] Convert H-tree boundary fallback to explicit relaxed policy or fail by default.
- [x] Rename report/visualization fallbacks to degraded output vocabulary where appropriate.
- [x] Replace adapter "caller may fallback" chains with typed result/provenance where practical.
- [x] Add or update focused tests for each behavior change.

## Phase 5: Data and Boundary Cleanup

- [x] Decide whether `ClockNetwork` becomes the stable semantic source for domain/inst/net roles.
- [x] Map duplicate role/domain enums and remove unnecessary conversions.
- [x] Keep `ClockLayout` as report/visualization projection only unless a confirmed stage contract requires it.
- [x] Reduce direct use of `FastStaAdapter::mutableClockContext` from flow code.
- [x] Replace repeated long parameter bundles with CTS-named input/context structs.
- [x] Audit module-level `STA_ADAPTER_INST` and `WRAPPER_INST` usages and either:
  - move adapter access to flow/database adapter boundaries; or
  - introduce explicit provider/options interfaces with local justification.

## Phase 6: Test Simplification

- [x] Consolidate normal flow validation into one flow test runner.
- [x] Split the default oversized flow test above 600 lines; remaining historical oversized real-tech/source files are recorded below as follow-up refactor backlog.
- [x] Remove duplicate or obsolete cases after mapping coverage to requirements.
- [x] Keep default/CI iCTS test targets fast, deterministic, and asset-independent.
- [x] Move real-tech smoke, real-tech regression, and benchmark tests to explicit opt-in/manual target families.
- [x] Remove hard-coded local benchmark paths or replace them with explicit environment-variable/repo-relative asset discovery.
- [x] Ensure missing real-tech/benchmark assets cause a clear skip or not-registered state, not local-machine fallback.
- [x] Keep fast unit tests in normal CI/CTest discovery.
- [x] Validate:
  - `ctest -N -R icts`
  - fast iCTS test executables;
  - selected real-tech tests only when assets are available.

## Final Validation

- [x] Build affected iCTS source targets.
- [x] Run representative fast tests.
- [x] Run full iCTS checker:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

- [x] Fix in-scope checker findings and rerun the same checker.
- [x] Summarize remaining risks, especially behavior-changing fallback/config decisions.

## Follow-up Phase: Runtime Semantic Boundaries

- [x] Apply user-confirmed policy that missing DBU-per-micron and missing/invalid RC routing layer are fatal at algorithm boundaries.
- [x] Keep `wire_width` optional so RC queries can use the technology/library default.
- [x] Allow char-only/unit tests to inject explicit synthetic DBU.
- [x] Keep fallible STA wire RC query facades for report/probe paths and add required wrappers for algorithm paths.
- [x] Convert fast STA, routing, STA RC-tree, cluster constraint, sink clustering, sink-load-region, root-driver compensation, H-tree/source-trunk, topology length, characterization, and analytical characterization paths away from sentinel DBU/layer/zero-RC behavior.
- [x] Remove production `fallback` wording from iCTS source, using degraded/normalized/explicit policy language instead.
- [x] Fix the fast STA `ClockLayout` RC extraction order so layout geometry is converted to RC only after runtime options are applied.
- [x] Rebuild the user-facing `iEDA` executable target.
- [x] Run the requested iCTS dev script:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Result: passed. CTS synthesis, optimization, instantiation, evaluation, and report all finished, and the run ended with `iCTS run successfully.`

- [x] Rerun full iCTS checker after semantic-boundary changes:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Result: passed with `In-scope findings: 0`.

## Execution Result

- Final full checker passed:
  `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`
  reported `In-scope findings: 0`.
- Default fast CTest discovery now reports 15 iCTS tests, and `ctest --test-dir build -R icts --output-on-failure` passed 15/15.
- Mechanical compliance scans:
  - old license wording count: 0;
  - missing `@author Dawn Li (dawnli619215645@gmail.com)` count: 0.
- Current line-count status:
  - `Optimization.cc` and the default `FlowTest.cc` runner were split below 600 lines;
  - historical oversized files remain as explicit follow-up backlog because splitting them safely requires independent semantic refactors, especially real-tech regression, H-tree, SDC, Wrapper, QorEvaluation, Schema, and routing files.
