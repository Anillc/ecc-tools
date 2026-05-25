# CTS contract polish and convergence

## Goal

Polish iCTS Input/Config/Output/Summary contracts after desingleton refactor, remove empty or redundant contracts, audit remaining module/subflow gaps, and re-run final validation including ecc_dev_tools.

## Follow-Up Boundary

Deeper structural optimization is tracked separately in `.trellis/tasks/05-25-cts-structural-optimization-refactor`. This current task must finish the
short-term convergence work first: make existing contracts strict, remove misleading wrappers, keep dependencies explicit, and keep production summaries
minimal. Do not fold the larger pipeline/build-commit/HTree lifetime redesign into this task unless it is required to satisfy the short-term acceptance
criteria below.

## Sequencing Decision

- Record structural optimization ideas here only as scope boundaries for the current convergence task.
- Keep implementation focus on this task until all current acceptance criteria, builds, tests, real-flow validation, and final `ecc_dev_tools` check pass.
- After this task completes, switch to `.trellis/tasks/05-25-cts-structural-optimization-refactor`.
- The structural optimization task must start from the post-convergence code, then perform its own audit, planning, implementation, review, and validation.

## Requirements

- Remove empty `{Name}Input`, `{Name}Config`, `{Name}Output`, and `{Name}Summary` structs from iCTS flow/module contracts.
- Remove wrapper-only contracts such as `{Name}Output { {Name}Summary summary; }`; use the semantically correct single return/argument type instead.
- Keep `{Name}Output` and `{Name}Summary` separate only when both are needed:
  - `Output` carries design payloads or values consumed by later stages.
  - `Summary` carries execution status, counters, warnings, logs, or metrics.
- Keep `{Name}Config` only for parameters that actually affect algorithm behavior and can vary at the algorithm/module boundary.
- Keep `{Name}Input` only when it groups meaningful stage inputs and improves readability over direct parameters.
- Convert remaining public flow/module stage facades that still expose long runtime dependency parameter lists into stable `{Name}Input` contracts.
  Do not add empty or fake `{Name}Config` contracts when a stage has no behavior-changing policy object.
- Use CTS-level `SchemaWriter` spelling in business signatures instead of leaking `schema::SchemaWriter` throughout flow/module contracts. Keep the
  `schema::` namespace for schema/report DSL types such as `StageReportOptions`, `KeyValueFields`, and emit helpers.
- Slim HTree and topology summaries so report-only or test-only HTree diagnostics are not transported through production `Summary` objects.
  Detailed HTree diagnostics should be emitted at the owning HTree/report stage or exposed through test-only observation paths.
- Audit production iCTS flow and module public contracts for remaining broad `Options`/`Result` style wrappers, and either converge them to the above contract vocabulary or document why they are local algorithm vocabulary rather than flow/module contracts.
- Preserve the desingleton architecture: no new singleton usage outside `CTSAPI`, no service locator, no global runtime registry, and no hidden algorithm dependencies.
- Preserve behavior and existing external flow outputs.
- Run final validation, with full `ecc_dev_tools` check only after the contract audit and code convergence are complete.

## Acceptance Criteria

- [x] No empty `Input`/`Config`/`Output`/`Summary` structs remain in production iCTS flow/module contracts.
- [x] No summary-only `Output` wrappers remain; summary-only stages return or pass their summary directly.
- [x] Public flow/module contract names follow the Input/Config/Output/Summary rules, with any remaining `Options`/`Result` names explicitly justified as local algorithm vocabulary.
- [x] Remaining public stage facades with multiple runtime/domain dependencies use named `{Name}Input` contracts rather than long parameter lists.
- [x] Business signatures use `SchemaWriter` rather than `schema::SchemaWriter`; `schema::` remains only for schema/report DSL types.
- [x] `HTreeSummary`, `Topology::Summary`, and `SourceTrunkSummary` do not carry broad report-only HTree diagnostics across module boundaries.
- [x] All touched call sites, tests, and CMake references build after the contract changes.
- [x] Architecture greps confirm no `_INST` singleton access remains in iCTS source/test except the allowed CTSAPI boundary.
- [x] Representative iCTS test suite and real design flow still pass.
- [x] `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` passes after all other convergence checks.

## Notes

- This is a convergence task on top of `05-24-cts-desingleton-refactor`; do not expand into unrelated cleanup.
- Structural redesign after this convergence pass belongs to `05-25-cts-structural-optimization-refactor` and must be based on a fresh audit of the
  post-convergence code.
