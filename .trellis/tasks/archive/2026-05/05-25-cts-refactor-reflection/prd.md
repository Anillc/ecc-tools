# CTS refactor reflection

## Goal

Reflect on the ongoing iCTS desingleton and contract cleanup work before committing it. The goal is to objectively analyze the current
uncommitted refactor, the newly identified contract gaps, and the first-principles architecture that would best optimize iCTS for readability,
maintainability, and clear CTS business semantics.

## Requirements

- Record this task first as a PRD/design reflection task before any additional source changes are made.
- Review the current uncommitted desingleton and contract-polish tasks as evidence, not as assumptions.
- Summarize what the current refactor improved and what it still gets wrong.
- Analyze the three newly identified gaps:
  - stage facades such as `Synthesis::run(...)` still expose long runtime dependency parameter lists instead of stable `{Name}Input` contracts;
  - business signatures expose `schema::SchemaWriter` namespace noise instead of a clear CTS-level `SchemaWriter` dependency name;
  - `HTreeSummary` and nested topology summaries transport too much report/test-only diagnostic data across module boundaries.
- Re-evaluate the `{Name}Input`, `{Name}Config`, `{Name}Output`, and `{Name}Summary` rules from first principles.
- Add a post-structural-refactor review of `FastSTA::buildClockContext`, especially duplicated overloads and repeated broad runtime dependency
  parameters (`Config`, `STAAdapter`, `Wrapper`).
- Reassess whether frequently reused runtime dependencies should be bound privately at runtime/stage construction boundaries instead of repeated in
  per-call algorithm APIs.
- Propose a better CTS architecture even if it means revising the current implementation direction.
- Keep the analysis objective: include advantages, disadvantages, risks, and migration implications.
- Do not modify iCTS source code in this task unless a later implementation task is explicitly approved.
- Record the sequencing decision: short-term contract corrections stay in `05-24-cts-contract-polish-convergence`; deeper structural optimization is
  deferred into a separate follow-up task that starts only after the short-term task is complete and validated.
- The follow-up task must begin from the post-short-term-refactor code shape, then perform its own audit, plan, implementation, review, and validation.

## Acceptance Criteria

- [x] `design.md` records the current-state assessment, including concrete evidence from the active tasks and code structure.
- [x] `design.md` states clear first-principles architecture rules for CTS ownership, dependency passing, mutation boundaries, reporting, and contracts.
- [x] The proposed architecture directly addresses readability, maintainability, and CTS business semantics.
- [x] The analysis distinguishes short-term fixes from longer-term architectural redesign.
- [x] The task identifies which changes should remain in the current contract-polish task and which should be separate follow-up work.
- [x] A separate structural optimization task is created with expected improvement areas and an explicit dependency on completing the current
  contract-polish task first.
- [x] The FastSTA clock-context API audit records which overloads are actually used and which public semantics appear redundant.
- [x] The design recommendation states when runtime dependencies should be private/bound versus explicit call parameters.

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- This is a planning/reflection task. Its output is a design recommendation, not a code diff.
- Follow-up structural optimization task: `05-25-cts-structural-optimization-refactor`.
- Follow-up runtime-boundary cleanup task: `05-25-cts-runtime-boundary-cleanup`.
