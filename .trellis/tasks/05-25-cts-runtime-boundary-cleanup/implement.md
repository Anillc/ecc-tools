# CTS runtime-bound service boundary cleanup implementation plan

## Planning Gate

This task is currently in planning. Do not start source edits until the PRD/design/implementation plan are reviewed and `task.py start` is run.

## Phase 1: Audit And Classification

- [ ] Re-run grep/audit for duplicated overloads and broad runtime dependency signatures in `src/operation/iCTS`.
- [ ] Classify findings into:
  - must clean in this task;
  - acceptable explicit configured adapter operation;
  - separate follow-up because it is larger than this task.
- [ ] Update `research/runtime-boundary-audit.md` with final classification before source edits.

Checkpoint:

```bash
rg -n "buildClockContext|Config.*Wrapper|Wrapper.*Config|Config.*STAAdapter|STAAdapter.*Config" src/operation/iCTS
```

## Phase 2: FastSTA Environment Binding

- [ ] Add `FastStaEnvironment`.
- [ ] Add `FastStaClockBuildInput`.
- [ ] Add `FastSTA::bindEnvironment(...)` or equivalent constructor/init-time binding.
- [ ] Convert `FastSTA::buildClockContext` to take only `FastStaClockBuildInput`.
- [ ] Remove or privatize unused public no-route build overload.
- [ ] Convert `FastStaBuilder` to consume `FastStaEnvironment` and `FastStaClockBuildInput`.
- [ ] Preserve direct synthetic-context registration tests where they remain useful.

Checkpoint:

```bash
ninja -C build icts_source_database_adapter_fast_sta icts_test_database_adapter_fast_sta
ctest --test-dir build -R '^icts_test_database_adapter_fast_sta$' --output-on-failure
```

## Phase 3: Optimization FastSTA Context Flow

- [ ] Derive `FastStaEnvironment` once at the optimization boundary.
- [ ] Build per-clock `FastStaClockBuildInput` from `Clock` and `ClockLayout` route geometry.
- [ ] Clarify route geometry vs route-tree injection semantics in code names.
- [ ] Centralize FastSTA context erase responsibility with a scoped guard or local per-clock run object.
- [ ] Preserve all current skip/failure branches.

Checkpoint:

```bash
ninja -C build icts_source_flow icts_test_flow
ctest --test-dir build -R '^icts_test_flow$' --output-on-failure
```

## Phase 4: Topology Helper Boundary Cleanup

- [ ] Refactor `BuildSinkHtreeInput` so it does not pass the whole runtime dependency list as separate parameters.
- [ ] Refactor `BuildTopSegmentInput` and `BuildTopHtreeInput` similarly.
- [ ] Keep per-call domain facts explicit: root net, source pin, root input, sink/source role, object prefix, log context.
- [ ] Avoid introducing a broad generic `Context` object; use a narrow local binding or existing stage input.

Checkpoint:

```bash
ninja -C build icts_source_flow_synthesis_topology icts_test_flow_synthesis
ctest --test-dir build -R 'icts_test_flow_synthesis|icts_test_flow_synthesis_htree' --output-on-failure
```

## Phase 5: Final Same-Class Sweep

- [ ] Re-run broad grep for repeated runtime dependency signatures.
- [ ] Verify every remaining hit is either expected or recorded.
- [ ] Confirm no `CTSRuntime&` appears below API/Flow ownership boundary.
- [ ] Confirm no `_INST` / `getInst()` usage outside `CTSAPI`.
- [ ] Confirm public overloads left in FastSTA/Topology/STAAdapter are semantically justified.

## Phase 6: Validation

- [ ] Targeted iCTS build.
- [ ] Full `icts_test_*` suite.
- [ ] `ninja -C build iEDA`.
- [ ] Real `ics55_dev` flow.
- [ ] Final `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`.

Final commands:

```bash
ninja -C build icts_source_flow icts_test_flow icts_test_flow_synthesis icts_test_database_adapter_fast_sta
ctest --test-dir build -R '^icts_test_' --output-on-failure
ninja -C build iEDA
cd scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Rollback Boundaries

- FastSTA API changes should be isolated enough to revert independently before topology helper cleanup.
- Optimization context-lifetime cleanup should be reverted if any skip/failure behavior changes unexpectedly.
- Topology helper cleanup should preserve HTree input/config values byte-for-byte in intent; if QoR changes, stop and inspect before continuing.
