# Implementation Plan · CTS 去单例化重构

## Gate

This parent task is still in planning. Do not start implementation until the artifacts are reviewed and the relevant parent/child task is moved to `in_progress`.

Implementation is split into child tasks:

1. `05-24-cts-runtime-flow-desingleton`
2. `05-24-cts-reporter-config-explicit`
3. `05-24-cts-design-wrapper-explicit`
4. `05-24-cts-sta-faststa-explicit`
5. `05-24-cts-synthesis-contract-cleanup`
6. `05-24-cts-flow-contract-tests-spec`

## Phase 0 · Baseline And Guardrails

- [x] Record current `_INST` counts with:
  `rg -n '\b[A-Z][A-Z0-9_]*_INST\b' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test`
- [x] Run baseline build/test chosen by the implementer, at minimum `bash build.sh`.
- [x] Add a temporary tracking note in the task for every remaining `_INST` macro after each phase.
- [x] Decide whether to split this task into child tasks before code starts. Created children:
  - `05-24-cts-runtime-flow-desingleton`
  - `05-24-cts-reporter-config-explicit`
  - `05-24-cts-design-wrapper-explicit`
  - `05-24-cts-sta-faststa-explicit`
  - `05-24-cts-synthesis-contract-cleanup`
  - `05-24-cts-flow-contract-tests-spec`

## Phase 1 · Runtime Owner And Flow

- [x] Add non-global `CTSRuntime` or equivalent owner for `Config`, `Design`, `Wrapper`, `STAAdapter`, `FastSTA`, and `SchemaWriter`.
- [x] Make `CTSAPI` own runtime + `Flow`.
- [x] Convert `Flow` from singleton to normal object.
- [x] Replace `FLOW_INST` call sites in `CTSAPI.cc`.
- [x] Keep public `CTSAPI` signatures stable.
- [x] Build after this phase.

Validation:

```bash
rg -n 'FLOW_INST|Flow::getInst' src/operation/iCTS
bash build.sh
```

## Phase 2 · Explicit Reporter

- [x] Convert `SchemaWriter` from singleton to runtime-owned reporter.
- [x] Replace `SCHEMA_WRITER_INST` in flow/setup/evaluation/report first.
- [x] Replace module-level reporter access in `TopologyGen`, `CharBuildOrchestrator`, `CharSetupConfigurator`, HTree submodules.
- [x] Replace `schema::Emit*` helpers with overloads that take `SchemaWriter&`, or use reporter member calls directly.
- [x] Update tests that open/close schema output.

Validation:

```bash
rg -n 'SCHEMA_WRITER_INST|SchemaWriter::getInst' src/operation/iCTS
bash build.sh
```

## Phase 3 · Explicit Config

- [x] Make `Config` a normal runtime-owned object.
- [x] Keep parser compatibility but stop reading `CONFIG_INST` below setup/config adaptation boundaries.
- [x] Create narrow flow/module config builders near the flow boundary.
- [x] Replace direct global reads in:
  - `Setup.cc`
  - `HTree.cc`
  - `Constraint.cc`
  - `Plan.cc`
  - `SinkBranch.cc`
  - `SourceTrunk.cc`
  - `SinkLoadClustering.cc`
  - `SinkLoadRegion.cc`
  - `CharacterizationLibrary.cc`
  - `Optimization.cc`
  - report path code
- [x] Remove fake config from algorithm contracts; preserve JSON parse compatibility only where needed.

Validation:

```bash
rg -n 'CONFIG_INST|Config::getInst' src/operation/iCTS/source src/operation/iCTS/api src/operation/iCTS/test
bash build.sh
```

## Phase 4 · Explicit Design And Wrapper

- [x] Make `Design` runtime-owned, not singleton.
- [x] Convert flow/tests to use explicit `Design&`.
- [x] Update `Wrapper` read/materialization/writeback methods to receive explicit `Design&` or explicit clock vectors from a known design.
- [x] Remove `DESIGN_INST` from wrapper, flow, topology, instantiation, evaluation, report visualization, and tests.
- [x] Preserve ownership rules: `Design` owns final CTS objects; temporary algorithm outputs own temporary objects until commit.

Validation:

```bash
rg -n 'DESIGN_INST|Design::getInst' src/operation/iCTS
rg -n 'WRAPPER_INST|Wrapper::getInst' src/operation/iCTS
bash build.sh
```

## Phase 5 · Explicit STA And FastSTA

- [x] Convert `STAAdapter` call sites to receive `STAAdapter&`.
- [x] Convert `FastSTA` mutable-context operations to instance methods on `FastSTA&`.
- [x] Update Characterization, HTree compensation, topology clustering, optimization, evaluation, and fast_sta builder code.
- [x] Document that explicit `STAAdapter&` does not imply iSTA is thread-safe.

Validation:

```bash
rg -n 'STA_ADAPTER_INST|STAAdapter::getInst|FAST_STA_INST|FastSTA::getInst' src/operation/iCTS
bash build.sh
```

## Phase 6 · Contract Cleanup

- [x] Replace `HTreeSynthesisOptions` with `HTreeInput` + `HTreeConfig` or equivalent.
- [x] Replace `HTreeSynthesisResult` with `HTreeOutput` + `HTreeSummary`.
- [x] Apply the same split to Topology sink branch/source trunk contracts.
- [x] Move DBU, clock period, object prefix, reporter, char library, STA adapter, and semantic role fields out of algorithm config.
- [x] Split Characterization runtime options into input/config and summary.
- [x] Split Optimization/Evaluation/Report outputs from summaries.
- [x] Avoid broad `Options` / `Result` structs that mix dependency, input, config, output, and report data.

Validation:

```bash
rg -n 'struct .*Options|struct .*Result|BuildOptions|BuildResult' src/operation/iCTS/source/flow src/operation/iCTS/source/module
bash build.sh
```

The validation command is an audit, not an absolute ban: remaining names must be justified or renamed to the new convention.

## Phase 7 · Test Refactor

- [x] Replace singleton reset fixtures with explicit runtime fixtures.
- [x] Add tests for two independent runtimes in one process.
- [x] Add or update HTree/Topology tests to pass explicit configs and fake/local reporter.
- [x] Keep real-tech fixtures explicit: config parse, wrapper init, STA init, design materialization.

Validation:

```bash
ninja -C build icts_test
# or the repo-specific iCTS test target list available in the build tree
```

## Phase 8 · Final Cleanup

- [x] Delete non-CTSAPI `_INST` macros and `getInst()` methods.
- [x] Delete dead singleton reset paths and includes.
- [x] Run grep acceptance.
- [x] Run representative iCTS flow:
  `cd scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
- [x] Run final check:
  `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`
- [x] Update specs.

Final acceptance grep:

```bash
rg -n '\b[A-Z][A-Z0-9_]*_INST\b' src/operation/iCTS/source src/operation/iCTS/test src/operation/iCTS/api
```

Expected: only `CTS_API_INST` remains, and internal source code does not depend on it.

## Risk Files

- `src/operation/iCTS/api/CTSAPI.cc`
- `src/operation/iCTS/source/flow/Flow.*`
- `src/operation/iCTS/source/flow/setup/Setup.*`
- `src/operation/iCTS/source/utils/logger/Schema.*`
- `src/operation/iCTS/source/database/config/Config.*`
- `src/operation/iCTS/source/database/design/Design.*`
- `src/operation/iCTS/source/database/io/Wrapper*`
- `src/operation/iCTS/source/database/adapter/sta/*`
- `src/operation/iCTS/source/database/adapter/fast_sta/*`
- `src/operation/iCTS/source/flow/synthesis/htree/*`
- `src/operation/iCTS/source/flow/synthesis/topology/*`
- `src/operation/iCTS/source/module/characterization/*`
- `src/operation/iCTS/test/**`

## Rollback Points

- After each macro family removal (`FLOW`, `SCHEMA`, `CONFIG`, `DESIGN/WRAPPER`, `STA/FAST_STA`), keep the build green before moving on.
- Avoid half-removing a singleton family across source and tests; finish one family or revert that family.
- If a flow cannot stay green while replacing reporter/config/design simultaneously, roll back to the previous family boundary and split the task.
