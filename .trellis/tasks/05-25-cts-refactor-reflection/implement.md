# CTS refactor reflection implementation plan

## Scope

This task is a planning and architecture reflection task. It does not modify iCTS source code directly.

## Evidence Reviewed

- `.trellis/tasks/05-24-cts-desingleton-refactor/prd.md`
- `.trellis/tasks/05-24-cts-desingleton-refactor/design.md`
- `.trellis/tasks/05-24-cts-desingleton-refactor/implement.md`
- `.trellis/tasks/05-24-cts-contract-polish-convergence/prd.md`
- `.trellis/tasks/05-24-cts-contract-polish-convergence/design.md`
- `.trellis/tasks/05-24-cts-contract-polish-convergence/research/contract-audit.md`
- `.trellis/spec/backend/database-guidelines.md`
- `.trellis/spec/backend/quality-guidelines.md`
- Current code evidence around `Synthesis::run`, `Topology::formClock`, `ClockDataRead::read`, `Setup`, `HTreeSummary`, `Topology::Summary`,
  and `schema::SchemaWriter` usage.
- `FastSTA::buildClockContext` and `FastStaBuilder::buildClockContext` overload usage, including the only production caller in
  `Optimization.cc`.
- `.trellis/tasks/05-25-cts-refactor-reflection/research/faststa-context-audit.md`

## Decisions

- Keep the desingleton direction.
- Revise the current contract-polish task before commit to address remaining public boundary issues.
- Treat `Input/Config/Output/Summary` as ownership and consumer contracts, not a mechanical naming scheme.
- Add `Environment` / `RuntimePolicy` as the right category for stable dependencies of stateful runtime services such as FastSTA.
- Do not leave duplicated public overloads when only one timing model is actually supported by the production flow.
- Move HTree report-only diagnostics out of production summary transport.
- Use separate follow-up tasks for deeper object-lifetime redesign such as per-build `HTreeBuilder`.

## Follow-Up Routing

Current contract-polish task should handle:

- stage input contracts for remaining long public facades;
- CTS-level `SchemaWriter` spelling in business signatures;
- slim HTree/Topology summaries;
- tests updated away from production summary internals.

Separate future tasks should handle:

- full per-clock synthesis pipeline redesign;
- per-build `HTreeBuilder` object internals;
- report/artifact based regression strategy for HTree diagnostics;
- typed stage policy extraction from global `Config`.
- FastSTA environment binding and a single `FastStaClockBuildInput` per-clock build API.

## Validation

For this reflection task:

- Review `prd.md` and `design.md` for consistency.
- No build or `ecc_dev_tools` run is required because this task does not edit source code.

For the current contract-polish implementation task after it absorbs short-term recommendations:

- targeted iCTS build;
- `ctest --test-dir build -R '^icts_test_' --output-on-failure`;
- real `ics55_dev` flow;
- final `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`.
