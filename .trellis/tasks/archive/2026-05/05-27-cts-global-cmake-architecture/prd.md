# Analyze and refactor CTS global CMake architecture

## Goal

Analyze and refactor the global CMake architecture under `src/operation/iCTS` so CTS CMake is easier to reason about, less order-sensitive, and more aligned with CTS source-layer ownership.

## Requirements

- Start from the repository-backed CMake architecture analysis already captured in `design.md`.
- Maintain a complete checklist of architecture issues and mark each item only after the corresponding remediation and validation are complete.
- Refactor CTS CMake and required include/header ownership until the checklist converges.
- Preserve the established iCTS source layers: `database`, `utils`, `module`, and `flow`.
- Keep dependencies target-based. Do not reintroduce explicit generated archive paths such as `${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/lib*.a`.
- Prefer local target ownership and correct `PUBLIC` / `PRIVATE` visibility over broad aggregate dependencies.
- Keep flow responsibilities aligned with `setup -> synthesis -> optimization -> instantiation -> evaluation -> report`.
- Treat H-tree, topology synthesis, trace/layout, characterization, routing, and evaluation as the highest-risk areas because they currently carry the densest cross-target dependencies.
- Preserve runtime behavior; this is an architecture/build-graph refactor, not an algorithm change.
- Validate the converged implementation with real design `ics55_dev` before running the final ecc dev checker.
- Run `ecc_dev_tools` only after `ics55_dev` passes.
- Leave the finished implementation uncommitted for user review.
- Do not modify project specs unless the work discovers a durable global development convention that belongs in `.trellis/spec`.

## Acceptance Criteria

- [ ] Current CTS CMake architecture is documented with concrete evidence from the repository.
- [ ] The analysis lists current strengths, weaknesses, and likely risk areas.
- [ ] A complete architecture remediation checklist exists in `implement.md`.
- [ ] Checklist items are checked only after the corresponding cleanup is implemented and validated.
- [ ] CTS CMake contains no explicit generated static archive path links.
- [ ] Internal implementation targets avoid broad aggregate dependencies where concrete targets can express ownership.
- [ ] Include roots and public headers are tightened without breaking self-contained headers.
- [ ] `iEDA` target builds successfully.
- [ ] Focused CTS/H-tree targets and tests pass.
- [ ] Real design `ics55_dev` CTS run passes.
- [ ] Final `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` passes with 0 in-scope findings.
- [ ] Final implementation remains uncommitted.

## Notes

- This task follows the completed CTS archive-link cleanup, which removed explicit static archive path hacks and made the current CMake graph fully target-based for the touched CTS paths.
- The user requested a comprehensive remediation checklist, full cleanup item by item, `ics55_dev` validation before ecc dev check, and no commit after completion.
