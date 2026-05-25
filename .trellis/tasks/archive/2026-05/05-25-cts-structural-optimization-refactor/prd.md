# CTS structural optimization refactor

## Goal

Perform the deeper iCTS structural optimization refactor after the short-term contract-polish task is complete. The goal is to improve readability,
maintainability, and CTS business semantics beyond contract naming by reshaping synthesis, HTree, topology, reporting, and commit boundaries around
clear domain lifetimes and responsibilities.

## Gate

This task is a follow-up planning task only until `.trellis/tasks/05-24-cts-contract-polish-convergence` is complete and validated. Do not start code
implementation here before the short-term task has passed its builds, tests, real flow validation, and final `ecc_dev_tools` check.

When the gate opens, this task starts from the short-term refactor's final code, not from the current intermediate dirty worktree. The first work in
this task is a fresh structural audit, then updated planning, then implementation.

## Requirements

- Do not start implementation until `05-24-cts-contract-polish-convergence` is complete and validated.
- Re-audit the code after short-term contract cleanup; do not assume the current uncommitted code shape will remain unchanged.
- Use `05-25-cts-refactor-reflection/design.md` as the starting architecture rationale.
- Preserve the desingleton architecture: `CTSAPI` is the only singleton boundary; do not introduce `CTSRuntime&` as a deep service locator.
- Optimize for readability and business semantics over mechanical abstraction.
- Clarify the synthesis pipeline around CTS domain operations, not generic service names.
- Separate build planning from design commit where it improves correctness and reviewability.
- Move HTree internal diagnostics into local build/report ownership rather than production summary transport.
- Rework tests so internal algorithm diagnostics are validated through report artifacts, final payloads, or explicit test-only observation hooks.
- Preserve existing external CTS behavior and reports unless a report change is explicitly accepted during planning.
- Before coding, create an updated `design.md` and `implement.md` based on the post-short-term codebase.
- Treat the work as three ordered stages after the gate opens: audit/summarize, plan/design, then implement/validate.

## Expected Improvement Points

- Reframe synthesis around a per-clock CTS pipeline with explicit domain steps rather than one broad orchestration surface.
- Separate algorithm build payloads from design/iDB commit mutation so failed builds do not leave ambiguous partial state.
- Re-evaluate whether HTree should become a short-lived build object that owns local diagnostics, report emission, and temporary candidate data.
- Keep report-only diagnostics close to the algorithm/report owner instead of carrying them through production `Output` or `Summary` contracts.
- Replace repeated broad runtime `Config` reads in lower modules with minimal typed policies derived near the flow boundary.
- Audit topology/source-trunk/HTree boundaries for business names that describe CTS intent, not generic transport mechanics.
- Rework tests that currently depend on internal summary transport so they validate through natural boundaries: committed design payloads, generated
  reports, metrics, or explicit test-only observation hooks.
- Decide whether structural work should be split into child tasks once the post-convergence code shape is known.

## Acceptance Criteria

- [x] The task remains in planning until the contract-polish task is complete.
- [x] A fresh post-contract-cleanup audit identifies actual structural hotspots and risky files.
- [x] `design.md` describes the target structural architecture, migration plan, and rollback boundaries.
- [x] `implement.md` splits the work into verifiable phases with build/test checkpoints.
- [x] Synthesis, topology, HTree, reporting, and commit boundaries read in CTS domain language.
- [x] Long-lived or broad hidden contexts are not introduced.
- [x] Targeted iCTS builds, representative tests, real design flow, and final `ecc_dev_tools` pass after implementation.

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Dependency: complete `05-24-cts-contract-polish-convergence` first.
- Reference: `.trellis/tasks/05-25-cts-refactor-reflection/design.md`.
