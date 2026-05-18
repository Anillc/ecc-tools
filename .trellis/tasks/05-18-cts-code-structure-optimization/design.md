# Design: CTS Code Structure Optimization

## Principles

1. Preserve CTS semantics first. Use CTS terms such as clock source, sink domain, downstream tree, source-to-root, root buffer, H-tree buffer, routing segment, and committed design object.
2. Do not add generic abstractions unless they remove real duplication or clarify a stage contract.
3. Flow code owns orchestration, lifecycle, config reads, singleton validation, reports, and committed mutation.
4. Adapter code owns external-tool data extraction and external timing/power semantics.
5. Module code should receive explicit CTS data/options and should not read runtime singletons or external tool APIs directly.
6. Data projections must have one owner and one purpose. Avoid broad snapshots that duplicate queryable CTS state.
7. No hidden algorithmic fallback. Any fallback that can change topology, timing, QoR, legality, or runtime must be either explicit user/config policy or a failure.
8. Report/visualization degradation is allowed only when labeled as degraded output and cannot feed algorithm decisions.
9. Each `.cc`/`.hh` file should stay below 600 lines. Prefer smaller files around 300 to 500 lines for hot review areas.

## Target CTS Flow Contract

Confirmed CTS flow:

```text
setup -> synthesis -> optimization -> instantiation -> evaluation -> report
```

Update `.trellis/spec/backend/directory-structure.md` during implementation to define `source/flow/optimization/` as a first-class stage:

- reads committed CTS design and fast STA adapter state;
- owns sizing policy, accepted mutations, and optimization reporting;
- delegates timing/power to fast STA;
- delegates pure search mechanics to narrow solver/policy helpers;
- does not duplicate final instantiation/evaluation/report responsibilities.

## Optimization Split

Keep `source/flow/optimization/Optimization.hh/.cc` as the public stage facade. Split implementation into internal files under `source/flow/optimization/`:

```text
source/flow/optimization/
  Optimization.hh/.cc          # public facade and stage lifecycle only
  OptimizationTypes.hh         # narrow internal data structs
  OptimizationOptions.hh/.cc   # optimizer-owned policy/options with documented defaults
  OptimizationPreparation.cc   # master inventory, route-tree cache, fast STA setup
  OptimizationSolver.cc        # exact and scalable solver loops
  OptimizationCandidates.cc    # frontier/window/action/batch generation
  OptimizationMutation.cc      # committed Design/ClockLayout mutation
  OptimizationReport.cc        # schema/log summary and profile emission
```

Expected boundaries:

- `Optimization.cc`: `Optimization::run`, stage begin/end, per-clock orchestration.
- `OptimizationOptions`: all currently hard-coded search budgets and thresholds, resolved from optimizer-owned defaults/policy. Do not expose these knobs through user/global `Config` in this refactor.
- `OptimizationPreparation`: `collectBufferMasterInfos`, `buildRouteTreeCache`, `injectRouteTrees`, cap/slew baseline collection.
- `OptimizationSolver`: `solveClock`, `solveClockScalable`, trial apply/restore, state improvement policy.
- `OptimizationCandidates`: topology index, frontier sinks, arrival window, scoring, batch generation.
- `OptimizationMutation`: final master changes, pin-name updates, `ClockLayout` update.
- `OptimizationReport`: runtime profile tables, transition summaries.

## Data Model Direction

Preferred source-of-truth shape:

- `Design` owns final `Clock`, `Inst`, `Pin`, and `Net`.
- `ClockNetwork` should be the stable committed CTS topology/role model if it is kept.
- `ClockDAG` should be the query/index view used for traversal and reachability.
- `ClockLayout` should remain a report/visualization projection, not an algorithm input unless there is no committed-design alternative.
- `FastStaClockContext` should be adapter-owned. Flow code should prefer facade query/update APIs over direct `mutableClockContext`.
- Optimization should use a narrow `OptimizationProblem`/`ClockSizingProblem` view built from committed CTS topology and fast STA IDs, not reuse report projections as its primary semantic model.

Do not merge everything into one giant database type. The goal is fewer overlapping semantic models, not a universal object.

## constexpr Policy

Keep local `constexpr` only for:

- sentinel IDs;
- unit conversion constants;
- array sizes tied to data layout;
- numerical method invariants copied from a reference algorithm;
- rendering style constants in visualization-only code;
- local test fixture values.

Move to optimizer-owned options/policy when the value affects:

- optimization runtime budget;
- candidate count;
- search breadth;
- QoR trade-off;
- topology selection;
- fallback selection;
- logging volume on production designs.

Do not add user-facing/global `Config` fields for optimization search knobs in this task. The immediate goal is to remove hidden file-local tuning by giving the optimizer one explicit internal policy surface. This keeps the public config small and leaves room for later adaptive tuning.

Required action for kept non-obvious constants:

- add a short comment naming the source of truth or algorithmic contract;
- avoid comments for self-evident constants such as `0.5` only when the variable name already explains the role.

## Fallback Policy

Confirmed default:

- Required DBU-per-micron, RC routing layer, and required STA/iDB adapter state in algorithm paths: `LOG_FATAL` / `LOG_FATAL_IF` at the first algorithm boundary.
- Other required algorithm inputs missing: `LOG_ERROR` plus safe return only when the caller has a real typed failure path and continuation cannot produce misleading CTS data; otherwise `LOG_FATAL`.
- Optional report/visualization data missing: emit degraded output with a name such as `degraded_pin_to_pin_segments`; avoid feeding it back into synthesis/optimization.
- User-selected relaxation: add explicit config such as `allow_boundary_relaxation` or `allow_auto_characterization_grid` and report it as a chosen policy, not as fallback.
- Adapter query alternatives: return typed source/provenance (`port_limit`, `table_axis`, `runtime_config`, `unavailable`) instead of scattering fallback chains at callers.

Confirmed behavior direction:

- `CharBuilder` auto-derived `wirelength_unit_um` should become explicit config or explicit opt-in auto mode.
- `HTree` strict-boundary infeasible selection should fail by default, unless user enables relaxed boundary selection.
- `FastStaChar` DBU fallback to 1000 should fail if DBU is required and unavailable.
- Routing layer default 1 should be explicit project default or an error.
- `wire_width` remains optional and uses the technology/library default in RC queries when unspecified.
- `STAAdapter::queryWireResistance` and `queryWireCapacitance` remain fallible facades for report/probe use. Algorithm paths use required wrappers such as `queryRequiredWireResistance` and `queryRequiredWireCapacitance`.
- `ClockLayout` geometry can feed fast STA optimization only after runtime DBU/routing-layer options are injected into the fast STA context. Raw graph construction must not compute RC from layout segments before those preconditions are validated.

## Parameter Aggregation

Use context structs only for repeated CTS bundles:

- `ClockFlowContext`: clock, clock index, clock layout, trace/status/report state.
- `SinkDomainBuildInput`: sink domain, domain prefix, sinks, valid sink count, root buffer spec.
- `ClockLayoutSegmentInput`: clock identity, net, role, sink domain, phase, topology depth/level.
- `OptimizationTrialContext`: clock id, buffers, cap baseline, slew baseline, target skew, options.
- `OptimizationClockContext`: fast STA clock id, route-tree cache, clock, layout, per-clock profile.

Avoid generic bags named only `Context` when the fields do not share a stable CTS concept.

## Test Direction

Test target policy:

- Restore `ctest` registration first.
- Keep one normal flow test executable as the primary flow runner.
- Keep focused database/module/utility executables for unit boundaries.
- Keep default/CI test registration fast, deterministic, and asset-independent.
- Move real-tech smoke, real-tech regression, and benchmark tests into explicit opt-in/manual target families.
- Remove hard-coded local paths from committed tests. Use environment variables, repo-relative fixtures, or skip/avoid registration with a clear asset requirement.
- Split tests above 600 lines into support helpers plus focused test files.

Manual real-tech and benchmark suites should not silently fall back to local-machine defaults. If required PDK/design assets are absent, the suite should produce a clear skipped/not-registered state with the missing asset requirement in the message.

Suggested normal fast validation set:

```bash
ninja -C build icts_test_flow icts_test_database_adapter_fast_sta icts_test_flow_synthesis icts_test_flow_synthesis_htree icts_test_module_characterization
./bin/icts_test_flow --gtest_color=no
./bin/icts_test_database_adapter_fast_sta --gtest_color=no
./bin/icts_test_flow_synthesis --gtest_color=no
./bin/icts_test_flow_synthesis_htree --gtest_color=no
./bin/icts_test_module_characterization --gtest_color=no
```

Final validation after implementation:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Rollback Shape

Split mechanical cleanup from behavioral cleanup:

- mechanical header/license/test-registration fixes can be reverted independently;
- optimization file split should preserve behavior before moving knobs;
- fallback policy changes should be feature-gated or committed in focused patches with targeted tests;
- test deletion/quarantine should be reviewed with before/after target lists.
