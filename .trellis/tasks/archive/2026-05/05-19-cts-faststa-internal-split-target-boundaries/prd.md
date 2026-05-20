# FastSTA internal split and target boundaries

## Goal

Keep `FastSta.hh/.cc` as the external FastSTA facade while splitting internal code into cohesive CTS submodules and CMake targets.

## Parent Task

`.trellis/tasks/05-19-cts-code-normalization-refactor-research`

## Scope

- `src/operation/iCTS/source/database/adapter/fast_sta`
- FastSTA CMake files.
- FastSTA tests affected by file moves or target boundaries.

## Requirements

- Keep FastSTA under `database/adapter/fast_sta`.
- Make `FastSta.hh/.cc` the external include surface.
- Split internals by CTS concepts: clock tree/state, Liberty model, clock-net parasitic data, timing, power, clock-sizing edits, segment
  characterization, and report data.
- Split `FastStaTypes.hh` only along stable concept boundaries.
- Do not expose internal headers through broad caller dependencies unless a test is directly validating that internal component.
- Do not use `Network`, `Session`, `Input`, `fallback`, or `rollback` in new names.

## Implementation Checklist

- [x] Inventory current FastSTA source files and includes.
- [x] Define subfolder and CMake target map.
- [x] Move files into semantic subfolders without behavior changes.
- [x] Split `FastStaTypes.hh` into CTS concept headers where stable.
- [x] Keep external callers including `FastSta.hh`.
- [x] Update internal tests to link internal targets only when necessary.

## Acceptance Criteria

- [x] `FastSta.hh/.cc` remains the external facade.
- [x] Internal FastSTA responsibilities are separated by semantic folders/targets.
- [x] External production callers do not include internal FastSTA concept headers directly.
- [x] FastSTA tests pass.

## Validation

```bash
ninja -C build icts_source_database_adapter_fast_sta icts_test_database_adapter_fast_sta
./bin/icts_test_database_adapter_fast_sta --gtest_color=no
```
