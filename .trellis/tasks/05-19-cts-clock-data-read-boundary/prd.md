# CTS clock-data read boundary

## Goal

Move pre-synthesis clock-data read responsibility out of instantiation design conversion and into `flow/setup/clock_data`, preserving behavior while
making the CTS stage boundary explicit.

## Parent Task

`.trellis/tasks/05-19-cts-code-normalization-refactor-research`

## Scope

- `src/operation/iCTS/source/flow/Flow.hh`
- `src/operation/iCTS/source/flow/Flow.cc`
- `src/operation/iCTS/source/flow/setup`
- `src/operation/iCTS/source/flow/instantiation/design_conversion/DesignConversionClockData.cc`
- CMake targets for affected flow subdirectories.
- Flow tests covering configured clock data and SDC-derived clock data.

## Requirements

- Place the new clock-data read code under `source/flow/setup/clock_data`.
- Keep post-synthesis inserted-object commit under instantiation.
- Preserve configured clock-net pair behavior.
- Preserve SDC clock trace behavior.
- Keep root flow naming explicit as `readClockData`.
- Do not use `Input` as a folder, type, or facade name.

## Implementation Checklist

- [x] Create `flow/setup/clock_data` folder and CMake target.
- [x] Move clock-data read implementation out of `instantiation/design_conversion`.
- [x] Keep design commit / inserted-object materialization separate from clock-data read.
- [x] Update includes and target links.
- [x] Update tests only where source moves require include/link changes.
- [x] Verify behavior with configured-clock and SDC-clock cases.

## Acceptance Criteria

- [x] No pre-synthesis clock-data read code remains owned by instantiation design conversion.
- [x] `setup/clock_data` owns configured-clock and SDC-derived clock data read behavior.
- [x] Instantiation owns only post-synthesis commit/writeback responsibilities.
- [x] No new vague engineering names are introduced.
- [x] Flow setup/instantiation tests pass.

## Validation

```bash
ninja -C build icts_source_flow_setup icts_source_flow_instantiation icts_test_flow
./bin/icts_test_flow --gtest_filter='*Clock*:*Sdc*:*Read*' --gtest_color=no
```

Validation completed on 2026-05-19:

- `ninja -C build icts_source_flow_setup icts_source_flow_instantiation icts_test_flow`
- `./bin/icts_test_flow --gtest_filter='*Clock*:*Sdc*:*Read*' --gtest_color=no`
- `./bin/icts_test_flow --gtest_color=no`
- `git diff --check`
