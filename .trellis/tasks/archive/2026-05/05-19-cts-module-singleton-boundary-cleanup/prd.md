# CTS module singleton boundary cleanup

## Goal

Move runtime singleton and external adapter reads out of CTS algorithm modules where practical, replacing them with explicit CTS data/options passed
from flow/database boundaries.

## Parent Task

`.trellis/tasks/05-19-cts-code-normalization-refactor-research`

## Scope

- `src/operation/iCTS/source/module/routing/router`
- `src/operation/iCTS/source/module/characterization`
- `src/operation/iCTS/source/module/analytical_characterization`
- `src/operation/iCTS/source/module/topology/cluster_constraints`
- Flow/database callers that construct explicit options.
- Affected module tests.

## Requirements

- New or changed module code should receive explicit CTS data/options.
- Flow/database/adapter boundaries may read `CONFIG_INST`, `WRAPPER_INST`, and `STA_ADAPTER_INST` when they own runtime setup or external queries.
- Start with `Router::buildRCTree`, because its no-options overload exposes a misleading contract.
- Preserve algorithm behavior and defaults.
- Avoid moving external adapter policy into modules.

## Implementation Checklist

- [x] Inventory singleton/adapter reads in scoped modules.
- [x] Remove or deprecate `Router::buildRCTree(clock_tree)` if it cannot build RC data without explicit options.
- [x] Add explicit `dbu_per_um`, routing layer, wire width, RC query data, cap/slew limits, and buffer catalog options where needed.
- [x] Update flow/database callers to construct and pass those options.
- [x] Migrate characterization direct singleton reads in focused slices.
- [x] Migrate analytical characterization and topology constraint reads in focused slices.
- [x] Add or update tests that do not require global runtime setup when possible.

## Acceptance Criteria

- [x] Module APIs express required CTS data/options at compile time for touched paths.
- [x] No touched module path silently recovers config or adapter data through runtime singletons.
- [x] Flow/database/adapter boundaries remain the owners of external runtime policy.
- [x] Routing and characterization tests pass.

## Validation

```bash
ninja -C build icts_source_module_routing icts_source_module_characterization icts_source_module_topology
ninja -C build icts_test_module_routing icts_test_module_characterization
./bin/icts_test_module_routing --gtest_color=no
./bin/icts_test_module_characterization --gtest_color=no
```
