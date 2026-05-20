# CTS代码规范化大规模重构调研

## Goal

Create and execute an evidence-backed refactor campaign for normalizing the iCTS codebase structure, especially the CTS flow layer,
module/database boundaries, FastSTA structure, CMake target granularity, and CTS-domain naming. The task began with architecture research and was
approved for implementation through independently tracked child tasks.

The refactor must improve code readability, maintainability, and semantic clarity without changing CTS behavior by accident.

## User-Observed Problems

- The flow layer may need a unified design pattern so every sub-flow follows a fixed lifecycle contract such as `init`, `run`, and `report`, with
  clear phase responsibility and behavior boundaries.
- `module/` and `flow/` submodule folder hierarchy should be unified and should use CTS-domain names instead of generic or service-style names.
- Broad copied-state names and generic terms such as `Internal`, `Support`, `Request`, and `Response` are hard for users to understand because
  they do not explain CTS business semantics.
- Some modules, especially `fast_sta`, expose too much structure from one folder/target. The intended direction is for the external surface to be
  concentrated in `FastSta.hh/.cc`, while internal responsibilities become cohesive submodules and CMake targets.
- Several large modules keep broad declarations in one `.hh`, implement part in `.cc`, and scatter other implementation parts into sibling `.cc`
  files. Large functional modules need framework-level responsibility design before further cleanup.

## Confirmed Facts And Closure State

- Active task directory: `.trellis/tasks/05-19-cts-code-normalization-refactor-research`.
- Current branch during research: `cts_refactor`.
- Working tree was clean before task creation.
- Relevant rules were read from:
  - `.trellis/spec/project-constraints.md`
  - `.trellis/spec/backend/directory-structure.md`
  - `.trellis/spec/backend/database-guidelines.md`
  - `.trellis/spec/backend/quality-guidelines.md`
  - `.trellis/spec/guides/cross-layer-thinking-guide.md`
  - `.trellis/spec/guides/code-reuse-thinking-guide.md`
- Current backend spec defines the accepted flow shape as:
  `setup -> synthesis -> optimization -> instantiation -> evaluation -> report`.
- Current post-campaign source/test inventory under `src/operation/iCTS`:
  - 451 `.cc` / `.hh` files.
  - 80,050 total `.cc` / `.hh` lines.
  - 343 source `.cc` / `.hh` files, 59,328 source lines.
  - 108 test `.cc` / `.hh` files, 20,722 test lines.
  - No source `.cc` / `.hh` file currently exceeds 600 lines.
  - Largest source files are `ClockTracePins.cc` at 592 lines, `FastSta.cc` at 587 lines, `BoundSkewTreeBalance.cc` at 583 lines,
    `SourceTrunkSegment.cc` at 574 lines, and `Embedding.cc` at 568 lines.
  - One test file is above 600 lines: `FastSTATest.cc` at 664 lines. Source cleanup remains the priority for this parent task.
- Initial largest direct source directories by local `.cc` / `.hh` lines:
  - `source/module/routing/bound_skew_tree`: 5,450 lines / 21 files.
  - `source/database/adapter/fast_sta`: 4,949 lines / 28 files.
  - `source/database/adapter/sdc`: 3,072 lines / 14 files.
  - `source/module/characterization`: 2,755 lines / 20 files.
  - `source/database/adapter/sta`: 2,408 lines / 10 files.
  - `source/database/design`: 2,281 lines / 12 files.
  - `source/module/topology/fast_clustering`: 2,079 lines / 12 files.
- Flow root facade files exist for all formal stages:
  - `source/flow/setup/Setup.hh/.cc`
  - `source/flow/synthesis/Synthesis.hh/.cc`
  - `source/flow/optimization/Optimization.hh/.cc`
  - `source/flow/instantiation/Instantiation.hh/.cc`
  - `source/flow/evaluation/Evaluation.hh/.cc`
  - `source/flow/report/Report.hh/.cc`
- Flow lifecycle and root-flow naming were normalized through `.trellis/tasks/05-19-cts-stage-responsibilities-flow-lifecycle`.
- Clock-data read ownership was moved to `source/flow/setup/clock_data` through `.trellis/tasks/05-19-cts-clock-data-read-boundary`.
- FastSTA was split by semantic submodules and CMake targets while keeping `FastSta.hh/.cc` as the external facade.
- Production callers were migrated away from broad FastSTA clock-context access through CTS-specific query/edit APIs.
- Touched module singleton/adapter reads were moved toward explicit CTS data/options at module boundaries.
- Broad source headers such as `FastStaTypes.hh` and `OptimizationTypes.hh` were split or removed; remaining compatibility headers only preserve
  include stability where needed.
- Source/test scans for prohibited copied-state and generic terms returned no textual matches.
- A parent follow-up removed four residual rollback-style FlowTest case names and revalidated `icts_test_flow`.
- Generic path scans found only `source/database/design/ClockNetwork.hh/.cc`, which is allowed because it matches the database clock-network
  concept confirmed by the user.
- `Network` remains as a local graph variable in `module/topology/mcf/MinCostFlow.hh`; this is LEMON min-cost-flow graph terminology, not a CTS
  clock-net/domain name.
- The fanout-4 runtime convergence issue discovered during validation is tracked and completed in
  `.trellis/tasks/05-20-cts-fanout4-optimization-runtime`.
- User feedback on naming direction:
  - Avoid replacing one vague engineering term with another. Names such as `rollback`, `fallback`, `Input`, and `Session` remain too generic unless
    the surrounding CTS term makes the responsibility concrete.
  - Avoid `Network` in new CTS names when it can be confused with CTS `Net` / clock-net semantics.
  - Prefer names that state the CTS object and action directly, such as clock-net membership, inserted-clock objects, route tree, RC tree, Liberty
    arc table, sink-domain build, or clock-sizing solve.
- User confirmed planning decisions:
  - Track the refactor as a parent task with child tasks and checkbox acceptance criteria; do not treat it as one fixed implementation round.
  - Continue the campaign until all source-priority cleanup tasks are clean.
  - Define each flow stage's responsibility, required data, produced state, behavior, and capability before introducing shared lifecycle conventions.
  - Put clock-data read work under `setup/clock_data`.
  - Start with flow lifecycle and clock-data read boundaries.
  - Keep FastSTA under `database/adapter/fast_sta` while its facade and internal boundaries are normalized.
  - Make `FastSta.hh/.cc` the external FastSTA include surface.
  - Narrow `queryClockContext` / `mutableClockContext` through CTS-specific query/edit APIs before restricting or removing direct context access.
  - Replace broad `ClockLayout` usage in FastSTA timing setup with narrower clock route geometry objects.
  - Move module singleton/adapter reads toward explicit CTS data/options.
  - Forbid vague engineering names in CTS source. If a CTS name cannot be determined, list it for user confirmation before implementation.
  - Clean test helper naming after source naming and source boundaries are stable.
  - Split implementation into child tasks.

## Requirements

- Produce a detailed functional map of the current iCTS code, covering API, flow, database, adapter, module, utility, and test layers.
- Document current CTS execution semantics and data flow from external API to iDB writeback and reports.
- Define each flow stage's responsibility boundaries first: required CTS data, produced CTS state/artifacts, owned behavior, allowed capabilities, and
  forbidden dependencies.
- After stage responsibilities are clear, introduce only the lifecycle conventions needed to make those boundaries consistent.
- Identify folder hierarchy and CMake target inconsistencies in `flow/`, `module/`, `database/adapter/fast_sta`, and tests.
- Identify generic or non-CTS-semantic naming problems, with examples and replacement direction.
- Treat naming cleanup as a domain-language design task, not a mechanical rename. Candidate names must avoid vague substitutes such as `rollback`,
  `fallback`, `Input`, `Session`, or `Network` unless no clearer CTS term exists.
- If a source rename cannot be expressed with a concrete CTS object/action term, record it as a naming question for user confirmation before editing.
- Analyze FastSTA's current responsibility boundary and propose a target structure where `FastSta.hh/.cc` is the facade and internals are cohesive
  submodules.
- Identify modules whose headers and source splits are mechanically smaller but still semantically broad.
- Preserve the current accepted stage order:
  `setup -> synthesis -> optimization -> instantiation -> evaluation -> report`.
- Do not start implementation in this task until the research, design, and execution plan are reviewed. This was satisfied before child
  implementation started.
- Structure and naming children must preserve runtime behavior unless a separate approved child task documents the behavior change. The fanout-4
  runtime convergence child is that separate task.

## Acceptance Criteria

- [x] `research.md` explains current iCTS functionality, responsibilities, semantics, algorithms, data flow, and issue inventory with concrete file
  evidence.
- [x] `design.md` defines stage responsibilities before lifecycle conventions, including required data, produced state/artifacts, behavior,
  capabilities, and forbidden dependencies.
- [x] `design.md` proposes a FastSTA target-folder split and facade/internal API boundary.
- [x] `design.md` proposes naming rules for replacing generic terms with CTS-domain semantics.
- [x] `implement.md` provides a child-task checklist and phase plan that can track progress until all source-priority cleanup tasks are clean.
- [x] `implement.md` includes validation commands and review / recovery gates.
- [x] Child task PRDs exist for each independently trackable deliverable and include concrete acceptance checkboxes.
- [x] The task remained in planning status until the user explicitly approved implementation.

## Confirmed Decisions

1. Use a task tree and acceptance checkboxes to track progress. Mark child tasks complete only when their own validation passes, and continue until
   the source-priority cleanup list is clean.
2. Do not model the work as a strict first/second implementation round. Use the child task order as a dependency and review sequence, then keep
   advancing the remaining cleanups.
3. Define stage responsibilities first, then add the smallest useful lifecycle convention.
4. Place clock-data read under `setup/clock_data`.
5. Start with stage responsibilities / flow lifecycle and clock-data read boundaries.
6. Keep FastSTA under `database/adapter/fast_sta` for this campaign unless a later user decision changes the location.
7. Make `FastSta.hh/.cc` the external FastSTA facade.
8. Narrow FastSTA context access through CTS-specific queries/edits before restricting or removing direct context APIs.
9. Replace `ClockLayout` as a FastSTA route-geometry data source with narrower clock route geometry objects.
10. Move module singleton/adapter access toward explicit CTS data/options.
11. Forbid vague engineering naming in CTS source. Use business/domain terms; unresolved names must be listed for user confirmation.
12. Clean test helper naming last, after source boundaries and names are stable.
13. Split implementation into child tasks and use task status plus checklist completion to track progress.

## Child Task Map

- [x] `.trellis/tasks/05-19-cts-stage-responsibilities-flow-lifecycle` — define stage responsibilities first, then normalize lifecycle conventions.
- [x] `.trellis/tasks/05-19-cts-clock-data-read-boundary` — move clock-data read ownership to `setup/clock_data`.
- [x] `.trellis/tasks/05-19-cts-faststa-internal-split-target-boundaries` — keep FastSTA location stable while splitting internal concepts and targets.
- [x] `.trellis/tasks/05-19-cts-faststa-facade-narrowing` — replace broad FastSTA clock context access with CTS-specific queries/edits.
- [x] `.trellis/tasks/05-19-cts-module-singleton-boundary-cleanup` — move module singleton/adapter reads toward explicit CTS data/options.
- [x] `.trellis/tasks/05-19-cts-semantic-source-naming-cleanup` — clean source names by CTS object/action, asking user when a name is uncertain.
- [x] `.trellis/tasks/05-19-cts-large-header-concept-split` — split broad headers by stable CTS concept boundaries.
- [x] `.trellis/tasks/05-19-cts-test-helper-semantic-cleanup` — clean test helper naming after source cleanup.
- [x] `.trellis/tasks/05-20-cts-fanout4-optimization-runtime` — stop fanout-4 exact solver area-recovery search after target skew is met.

## Final Integration State

- All 9 child tasks are marked completed.
- Focused child validations and full built iCTS ctest coverage passed during the campaign, including `15/15` iCTS tests in the final test-helper
  child and the fanout-4 runtime child.
- `ics55_dev` binary validation passed for both fanout 4 and fanout 32 in the fanout-runtime child.
- Final `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` passed with `0` in-scope findings after fixing the final
  format, CMake visibility, and IWYU findings.
- No commit or archive has been made for this parent task.

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
