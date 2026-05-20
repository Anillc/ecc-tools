# CTS stage responsibilities and flow lifecycle

## Goal

Define and implement the CTS flow-stage responsibility model before introducing lifecycle conventions. The flow layer should make each stage's
required data, produced state/artifacts, behavior, capabilities, and forbidden dependencies clear from its facade and root-flow calls.

## Parent Task

`.trellis/tasks/05-19-cts-code-normalization-refactor-research`

## Scope

- `src/operation/iCTS/source/flow/Flow.hh`
- `src/operation/iCTS/source/flow/Flow.cc`
- `src/operation/iCTS/source/flow/setup`
- `src/operation/iCTS/source/flow/synthesis`
- `src/operation/iCTS/source/flow/optimization`
- `src/operation/iCTS/source/flow/instantiation`
- `src/operation/iCTS/source/flow/evaluation`
- `src/operation/iCTS/source/flow/report`
- Flow tests affected by facade/result naming.

## Requirements

- Preserve the accepted stage order: `setup -> synthesis -> optimization -> instantiation -> evaluation -> report`.
- Treat `setup/clock_data` as a setup substage, not a separate top-level stage.
- Define stage responsibilities before adding shared lifecycle helpers.
- Prefer convention and typed result structs over a virtual framework unless duplicated code proves a helper is necessary.
- Keep external `CTSAPI` behavior stable.
- Do not introduce vague names such as `Input`, `Session`, `Network`, `fallback`, or `rollback`.

## Implementation Checklist

- [x] Verify current root `Flow` and stage facade APIs.
- [x] Document or encode each stage's required CTS data.
- [x] Document or encode each stage's produced CTS state/artifacts.
- [x] Document or encode each stage's owned behavior and allowed capabilities.
- [x] Document or encode forbidden dependencies for each stage.
- [x] Rename root-flow helpers where needed, such as `readClockData`, `runSynthesis`, `runOptimization`, `instantiateClockTree`, `evaluateClockTree`,
  and `emitReports`.
- [x] Add typed stage results where current boolean state is ambiguous.
- [x] Keep behavior and report output stable.

## Acceptance Criteria

- [x] A reviewer can infer every flow stage's responsibility from facade/root-flow code without reading unrelated implementation files.
- [x] Root-flow method names match CTS stage behavior.
- [x] Stage status/result handling no longer relies on ambiguous local booleans where a typed stage result is needed.
- [x] No new generic engineering names are introduced.
- [x] Flow tests and affected build targets pass.

## Validation

```bash
ninja -C build icts_source_flow icts_test_flow
./bin/icts_test_flow --gtest_color=no
```

Validation completed on 2026-05-19:

- `ninja -C build icts_source_flow icts_test_flow`
- `./bin/icts_test_flow --gtest_color=no`
- `git diff --check`
