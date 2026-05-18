# CTS code structure optimization

## Goal

Optimize the iCTS code structure through an evidence-based refactor plan that preserves CTS semantics, removes accidental complexity, and makes future CTS optimization work easier to review and validate.

The task implementation is complete for the original approved refactor scope. The remaining historical files over 600 lines are tracked in
`research.md` as semantic-refactor follow-up work rather than mechanically split in this pass. A follow-up behavior-semantics pass has now
tightened DBU/routing-layer/required-RC boundaries in algorithm paths.

## Confirmed Facts

- Relevant project rules were read from `.trellis/spec/project-constraints.md` and `.trellis/spec/backend/*`.
- Backend rules apply to `src/operation/iCTS/`.
- The current backend directory spec documents the flow as `setup -> synthesis -> instantiation -> evaluation -> report`, while the current implementation and recent task designs already include `flow/optimization` as a first-class stage between synthesis and instantiation. This is a spec/code drift that must be resolved before broad refactoring.
- Current branch: `cts_refactor`.
- Current working tree before task creation was clean.
- New task directory: `.trellis/tasks/05-18-cts-code-structure-optimization`.
- iCTS has 366 `.cc`/`.hh` files and about 75.7k source/test lines under `src/operation/iCTS`.
- 19 iCTS `.cc`/`.hh` files exceed the requested 600-line ceiling. The largest is `src/operation/iCTS/source/flow/optimization/Optimization.cc` at 2100 lines.
- `constexpr` appears in 75 files and 389 matched lines: 202 in `source/`, 187 in `test/`.
- `fallback` wording appears in 182 matched source lines and 60 matched test lines.
- 103 iCTS files still contain `MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.` instead of the required `MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.`
- 3 iCTS files are missing the required `@author Dawn Li (dawnli619215645@gmail.com)` line.
- Current build tree has iCTS test targets, but `ctest -N -R icts` reports `Total Tests: 0`; test registration is not working even though individual test executables build and run.
- Representative test targets built and passed locally:
  - `icts_test_flow`: 26 tests passed.
  - `icts_test_database_adapter_fast_sta`: 7 tests passed.
  - `icts_test_flow_synthesis`: 14 tests passed.
  - `icts_test_flow_synthesis_htree`: 8 tests passed.
  - `icts_test_module_characterization`: 17 tests passed.
- User-confirmed policy: algorithmic fallback defaults to failure unless an explicit opt-in config/policy enables relaxed or auto-derived behavior. H-tree boundary relaxation, CharBuilder wirelength auto-derivation, FastStaChar DBU fallback, and routing-layer fallback should not silently continue by default.
- User-confirmed architecture: `flow/optimization` is a formal CTS flow stage between synthesis and instantiation. The accepted flow contract is `setup -> synthesis -> optimization -> instantiation -> evaluation -> report`.
- User-confirmed optimization-knob policy: do not expose optimization search knobs to user/global `Config` for this refactor. Consolidate algorithm-related parameters into optimizer-owned `Options` so the optimizer has one explicit internal policy surface, while leaving room for future adaptive tuning.
- User-confirmed test-scope policy: default/CI iCTS tests should keep only fast, deterministic synthetic/unit/flow coverage. Real-tech smoke, real-tech regression, and benchmark tests should move to explicit opt-in/manual target families with clear asset requirements; missing assets should skip clearly or avoid registration rather than masquerading as normal tests.
- Final implementation result: full `src/operation/iCTS` checker reports `In-scope findings: 0`, and default CTest discovery/runs pass 15/15 iCTS tests.
- Follow-up boundary audit policy from backend specs: `LOG_FATAL`/`LOG_FATAL_IF` is required when a required pointer/resource is missing and continuation would be misleading or unsafe; `LOG_ERROR` plus a safe default is valid only when the caller can continue safely.
- Follow-up boundary audit finding: DBU-per-micron, positive RC routing layer, initialized STA/iDB adapter, and speculative-mutation restore are required algorithm/adapter invariants. Returning `0`, clamping DBU to `1`, or continuing with zero RC can hide invalid CTS state in synthesis, RC-tree construction, fast STA, and optimization paths.
- Follow-up boundary audit finding: report/evaluation/visualization paths may continue with explicit degraded diagnostics, but degraded/sentinel values must not feed algorithm decisions.
- User-confirmed behavior-semantics policy:
  - missing DBU-per-micron and missing/invalid RC routing layer should be fatal at algorithm boundaries, including paths that previously returned typed per-clock failure for those global prerequisites;
  - `wire_width` remains optional and may use the technology/library default in RC queries;
  - char-only and unit tests may explicitly inject synthetic DBU instead of relying on production DBU fallback;
  - `STAAdapter::queryWireResistance` / `queryWireCapacitance` remain fallible for report/probe paths, while algorithm callers use required wrappers.

## Requirements

- Produce a structured issue inventory for CTS code structure, coding-standard drift, data model boundaries, parameter-list complexity, constexpr usage, fallback behavior, file-size violations, headers/license compliance, and tests.
- Preserve CTS semantics and avoid broad service-style abstractions that obscure clock-tree concepts.
- Treat flow-layer code as orchestration over explicit stage contracts. Algorithm modules should receive explicit options/data instead of reading runtime singletons directly.
- Reconcile the accepted CTS flow shape with project specs, especially whether `flow/optimization` is a formal stage.
- Consolidate duplicated or overlapping CTS data structures only when there is a clear stage contract and measurable simplification.
- Keep source and test files at or below 600 lines after refactoring.
- Classify all `constexpr` usages:
  - keep true invariants/unit conversions/sentinel values;
  - move optimization algorithm knobs to optimizer-owned `Options` when they affect QoR, runtime, search breadth, solver path, or fallback behavior;
  - do not add new user-facing config fields for optimization search knobs in this task unless separately approved later;
  - add comments for non-obvious invariants that must remain local constants.
- Replace ambiguous fallback behavior with one of:
  - explicit user/config option;
  - typed degraded diagnostic for report/visualization-only paths;
  - `LOG_ERROR`/safe return;
  - `LOG_FATAL` when continuing would hide an invalid CTS state.
- Clarify fatal-vs-recoverable behavior boundaries:
  - required global invariants such as DBU-per-micron, RC routing layer, adapter readiness, and mutation rollback must fail fast at algorithm/adapter build boundaries when missing or invalid;
  - shared query facades may remain fallible when they also serve report/degraded paths, but algorithm callers must immediately translate missing required values into `LOG_FATAL` or typed stage failure;
  - avoid algorithm-path sentinels such as `dbu = 0`, `routing_layer = 0`, zero RC from unavailable infrastructure, or `std::max(dbu, 1)` masking;
  - keep report/evaluation/visualization degradation explicit and isolated from synthesis/optimization decisions.
- Fix the required license wording and missing author metadata in iCTS files.
- Simplify tests:
  - consolidate flow tests so normal flow behavior can be run through one clear flow test target;
  - keep unit tests focused at database/module/utility boundaries;
  - keep default/CI tests fast, deterministic, and asset-independent;
  - move real-tech smoke, real-tech regression, and benchmark tests to explicit opt-in/manual target families;
  - require environment variables, repo-relative fixtures, or explicit asset discovery for real-tech/manual suites;
  - remove or quarantine stale, environment-specific, redundant, or obsolete tests;
  - make test registration visible through `ctest`.
- Define validation commands for each refactor phase, including representative fast tests and final full iCTS quality check.

## Acceptance Criteria

- [ ] `research.md` contains an evidence-backed issue list with file paths, counts, and severity.
- [ ] `design.md` defines CTS structure principles, target module boundaries, fallback/config policy, and test policy.
- [ ] `implement.md` provides an ordered, reviewable execution plan with validation commands.
- [ ] The user has confirmed open policy decisions before implementation starts.
- [ ] The implementation plan keeps file splits below 600 lines per `.cc`/`.hh` file.
- [ ] The plan identifies which `constexpr` values stay local, which move to optimizer-owned options, and which need explanatory comments.
- [ ] The plan identifies which fallback behavior remains allowed and which must become explicit failure or explicit config.
- [ ] The fatal/error boundary audit identifies candidate code paths and classifies each as fatal, typed stage failure, or degraded diagnostic.
- [ ] The user has confirmed DBU/routing-layer/wire-width policy before behavior changes are implemented.
- [ ] The implementation plan removes or guards algorithm-path sentinel unit/RC values without changing report-only degraded behavior.
- [ ] The plan includes mechanical cleanup for license wording and missing author lines.
- [ ] The plan includes test target cleanup and `ctest` registration repair.
- [ ] The plan separates default fast tests from manual/opt-in real-tech and benchmark suites.
- [ ] Final implementation, when approved later, runs:
  - targeted builds for affected iCTS targets;
  - representative fast iCTS tests;
  - `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`.

## Out of Scope

- QoR algorithm redesign beyond structural cleanup needed to preserve current behavior.
- Runtime/performance tuning beyond removing accidental structural overhead.
- Broad edits to external modules such as iSTA, iDB, or iPA, unless required by a CTS boundary bug.
- Changing benchmark scripts or real-tech assets except to remove hard-coded local paths from tests.

## Resolved Decisions

- Missing DBU-per-micron and missing/invalid RC routing layer are fatal at algorithm/adapter boundaries.
- `wire_width` remains optional and uses the technology/library default when unspecified.
- Char-only/unit tests may explicitly inject synthetic DBU.
- STA wire RC facades remain fallible for report/probe paths; algorithm paths use required-call wrappers.

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
