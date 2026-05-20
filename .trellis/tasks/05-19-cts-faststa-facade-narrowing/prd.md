# FastSTA facade narrowing

## Goal

Replace production dependency on broad `FastStaClockContext` access with CTS-specific FastSTA query and edit APIs.

## Parent Task

`.trellis/tasks/05-19-cts-code-normalization-refactor-research`

## Scope

- `src/operation/iCTS/source/database/adapter/fast_sta/FastSta.hh`
- `src/operation/iCTS/source/database/adapter/fast_sta/FastSta.cc`
- FastSTA internal implementation needed by facade APIs.
- `src/operation/iCTS/source/flow/optimization`
- `src/operation/iCTS/source/module/characterization`
- Tests affected by context-access migration.

## Requirements

- Migrate production callers away from `queryClockContext` and `mutableClockContext`.
- Add CTS-specific APIs for timing queries, cap/slew legality, power, route geometry, clock-net parasitic data, and clock-sizing edits.
- Replace broad `ClockLayout` timing setup usage with narrower clock route geometry objects.
- Restrict direct mutable context access to tests or remove it after production callers are migrated.
- Do not expose raw vectors/maps or implementation IDs outside the facade unless explicitly required by a domain view.

## Implementation Checklist

- [x] Inventory all `queryClockContext` and `mutableClockContext` callers.
- [x] Group caller needs by CTS operation.
- [x] Add narrow FastSTA query/edit APIs.
- [x] Migrate optimization callers.
- [x] Migrate characterization callers.
- [x] Add clock route geometry data object if needed.
- [x] Restrict or remove direct context access.

## Acceptance Criteria

- [x] Production flow/module code no longer depends on mutable FastSTA clock context layout.
- [x] FastSTA facade exposes CTS operations rather than raw internal storage.
- [x] Broad `ClockLayout` coupling is removed from new FastSTA timing setup path.
- [x] FastSTA and affected optimization/characterization tests pass.

## Validation

```bash
ninja -C build icts_source_flow_optimization icts_source_module_characterization icts_test_database_adapter_fast_sta
./bin/icts_test_database_adapter_fast_sta --gtest_color=no
```
