# CTS runtime-bound service boundary cleanup

## Goal

Clean up the class of CTS architecture issues exposed by `FastSTA::buildClockContext`: stable runtime dependencies are still passed as noisy
per-call arguments, duplicated public overloads imply unsupported semantics, and some topology/adapter helpers still move broad runtime plumbing
instead of explicit CTS-domain input.

The goal is not another mechanical rename pass. The goal is to make CTS code read around clear business boundaries: runtime-bound services bind
stable environment once; per-clock/per-algorithm calls pass only the data that actually varies.

## Requirements

- Start from the current post-desingleton / post-structural-refactor codebase.
- Treat the issue as a shared CTS boundary pattern, not only a FastSTA special case.
- First audit all iCTS interfaces and private helpers for:
  - duplicated public overloads whose production semantics are not both used;
  - repeated stable runtime dependencies passed as per-call arguments;
  - broad `Wrapper` use where only DBU or geometry facts are needed;
  - broad `Config` use where a typed policy/environment is clearer;
  - helper APIs that still expose long `Config / Design / Wrapper / STAAdapter / FastSTA / SchemaWriter` parameter lists.
- For each finding, either refactor it or explicitly classify why it is not the same problem.
- Prioritize high-signal production paths:
  - `FastSTA::buildClockContext`;
  - `FastStaBuilder::buildClockContext`;
  - `Optimization` per-clock FastSTA context setup/lifetime;
  - topology sink/source HTree input construction helpers;
  - STA/FastSTA configured runtime policy extraction.
- Introduce a bound runtime environment / policy concept where it improves readability and correctness.
- Do not reintroduce singleton access. `CTSAPI` remains the only singleton boundary.
- Do not pass `CTSRuntime&` into algorithms or lower services as a service locator.
- Do not hide per-clock/per-build data inside long-lived service state.
- Preserve current CTS external behavior, reports, QoR intent, and real-flow script compatibility unless an intentional report/API change is
  explicitly recorded.

## Architectural Requirements

The first-principles CTS architecture from the reflection task is binding for this cleanup, not just background rationale.

- Treat CTS as a domain pipeline:
  `constraints + tech + existing design -> clock intent/timing model -> synthesized topology -> committed CTS design objects -> iDB/timing projection -> evaluation/reports`.
- Every interface changed in this task must make ownership, mutation rights, downstream consumption, behavior decisions, environment facts, payloads,
  and diagnostics clear from the type names and parameter list.
- `CTSRuntime` is an owner at API/Flow setup boundaries only. It must not become a service locator passed into synthesis, optimization, FastSTA,
  topology, HTree, or adapter helpers.
- Runtime-bound domain services may bind stable service-specific environment once. The environment must be typed and narrow, not a generic
  `Context`, `Session`, or runtime bundle.
- Per-call `Input` must contain operation-varying CTS domain data only. Stable runtime facts do not belong in per-call input just because they are
  needed by the implementation.
- `Config` or `Policy` objects must contain behavior-changing knobs only. Global configuration objects should be narrowed before crossing into
  lower services or algorithms.
- `Output` should carry downstream design payload only. `Summary` should carry caller-relevant status/metrics only. Report-only or test-only
  diagnostics should stay local, be emitted directly by the owning module, or use an explicit diagnostic/test path.
- Short-lived builders are allowed when they make a build boundary clearer, but long-lived services must not hide per-clock/per-build state that
  should remain visible at the call site.
- Build and commit/mutation boundaries must remain explicit. Algorithm objects may build CTS-domain payloads; committing into `Design`, iDB, or
  timing state must be visible in the stage/service boundary.
- Public APIs must use CTS business language and represent real supported production semantics. Redundant overloads, compatibility shells, and
  implementation-shaped names should be removed, privatized, or recorded as intentional exceptions.

## Target Cleanup Rules

- Stateful runtime services may bind stable dependencies once through a typed environment.
- Per-call `Input` contains only the domain data that changes for that operation.
- `Config` remains the global parsed configuration at the flow boundary; lower modules receive typed policies when they need only a subset.
- `Wrapper` should not cross into lower modules when only `dbu_per_um` or geometry facts are required.
- Public overloads must correspond to real supported semantics and active callers; otherwise remove, privatize, or convert to test-only helpers.
- Private helpers should not recreate long public parameter lists. Prefer local bound objects, derived policies, or passing an existing stage input.

## Acceptance Criteria

- [ ] A fresh audit records all current same-class findings and non-findings under the task research directory.
- [ ] `FastSTA::buildClockContext` no longer exposes duplicated public build overloads with broad runtime dependencies.
- [ ] FastSTA clock context construction uses a typed bound environment or equivalent policy, not repeated `Config / STAAdapter / Wrapper` per call.
- [ ] The production FastSTA timing model is explicit: route-geometry build and route-tree injection responsibilities are not ambiguous.
- [ ] Optimization owns FastSTA clock context lifetime clearly, with context cleanup centralized or otherwise provably complete on all branches.
- [ ] Topology sink/source HTree input helper signatures no longer pass the full runtime dependency list when the enclosing topology input or a bound
  local environment can express the same data.
- [ ] Broad `Wrapper` dependencies below flow/stage boundaries are replaced by narrow facts where practical.
- [ ] Broad `Config` dependencies in touched FastSTA/Topology/Optimization code are replaced by typed policy/config objects where practical.
- [ ] Remaining `Config` / `Wrapper` / `STAAdapter` uses are reviewed and either intentionally retained with clear semantics or listed as separate
  follow-up work.
- [ ] All touched interfaces are checked against the first-principles architecture rules above, with deviations recorded in the task research notes.
- [ ] New abstractions use service-specific CTS domain names rather than broad generic runtime names such as `Context`, `Session`, or `Manager`
  unless the name already has established local meaning and a narrow scope.
- [ ] No report-only or test-only diagnostic data is moved into production `Output` / `Summary` just to satisfy tests.
- [ ] Build-versus-commit boundaries remain explicit for `Design`, iDB, and timing/FastSTA state mutation.
- [ ] No `CTSRuntime&` deep service-locator dependency is introduced.
- [ ] No singleton access is reintroduced outside the existing `CTSAPI` boundary.
- [ ] Targeted iCTS builds and representative tests pass.
- [ ] Real `ics55_dev` CTS flow passes.
- [ ] Final `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` reports 0 in-scope findings.

## Notes

- Reflection source: `.trellis/tasks/05-25-cts-refactor-reflection/design.md`.
- FastSTA-specific audit source: `.trellis/tasks/05-25-cts-refactor-reflection/research/faststa-context-audit.md`.
- This task should remain in planning until design and implementation artifacts are reviewed.
